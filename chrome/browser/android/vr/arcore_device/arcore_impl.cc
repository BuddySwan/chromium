// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr/arcore_device/arcore_impl.h"

#include "base/android/jni_android.h"
#include "base/bind.h"
#include "base/numerics/math_constants.h"
#include "base/optional.h"
#include "base/trace_event/trace_event.h"
#include "base/util/type_safety/pass_key.h"
#include "chrome/browser/android/vr/arcore_device/arcore_java_utils.h"
#include "chrome/browser/android/vr/arcore_device/arcore_plane_manager.h"
#include "chrome/browser/android/vr/arcore_device/type_converters.h"
#include "device/vr/public/mojom/vr_service.mojom.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "ui/display/display.h"
#include "ui/gfx/geometry/point3_f.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/transform.h"

using base::android::JavaRef;

namespace {

// Helper, returns new VRPosePtr with position and orientation set to match the
// position and orientation of passed in |pose|.
device::mojom::VRPosePtr GetMojomVRPoseFromArPose(const ArSession* session,
                                                  const ArPose* pose) {
  device::mojom::VRPosePtr result = device::mojom::VRPose::New();
  std::tie(result->orientation, result->position) =
      device::GetPositionAndOrientationFromArPose(session, pose);

  return result;
}

// Helper, creates new ArPose* with position and orientation set to match the
// position and orientation of passed in |pose|.

ArTrackableType GetArCoreEntityType(
    device::mojom::EntityTypeForHitTest entity_type) {
  switch (entity_type) {
    case device::mojom::EntityTypeForHitTest::PLANE:
      return AR_TRACKABLE_PLANE;
    case device::mojom::EntityTypeForHitTest::POINT:
      return AR_TRACKABLE_POINT;
  }
}

std::set<ArTrackableType> GetArCoreEntityTypes(
    const std::vector<device::mojom::EntityTypeForHitTest>& entity_types) {
  std::set<ArTrackableType> result;

  std::transform(entity_types.begin(), entity_types.end(),
                 std::inserter(result, result.end()), GetArCoreEntityType);

  return result;
}

// Helper, computes mojo_from_input_source transform based on mojo_from_viever
// pose and input source state (containing input_from_pointer transform, which
// in case of input sources is equivalent to viewer_from_pointer).
base::Optional<gfx::Transform> GetMojoFromInputSource(
    const device::mojom::XRInputSourceStatePtr& input_source_state,
    const gfx::Transform& mojo_from_viewer) {
  if (!input_source_state->description ||
      !input_source_state->description->input_from_pointer) {
    return base::nullopt;
  }

  gfx::Transform viewer_from_pointer =
      *input_source_state->description->input_from_pointer;

  return mojo_from_viewer * viewer_from_pointer;
}

void ReleaseArCoreCubemap(ArImageCubemap* cube_map) {
  for (auto* image : *cube_map) {
    ArImage_release(image);
  }

  memset(cube_map, 0, sizeof(*cube_map));
}

void CopyArCoreImage_RGBA16F(const ArSession* session,
                             const ArImage* image,
                             int32_t plane_index,
                             std::vector<device::RgbaTupleF16>* out_pixels,
                             uint32_t* out_width,
                             uint32_t* out_height) {
  // Get source image information
  int32_t width = 0, height = 0, src_row_stride = 0, src_pixel_stride = 0;
  ArImage_getWidth(session, image, &width);
  ArImage_getHeight(session, image, &height);
  ArImage_getPlaneRowStride(session, image, plane_index, &src_row_stride);
  ArImage_getPlanePixelStride(session, image, plane_index, &src_pixel_stride);

  uint8_t const* src_buffer = nullptr;
  int32_t src_buffer_length = 0;
  ArImage_getPlaneData(session, image, plane_index, &src_buffer,
                       &src_buffer_length);

  // Create destination
  *out_width = width;
  *out_height = height;
  out_pixels->resize(width * height);

  // Fast path: Source and destination have the same layout
  bool const fast_path = static_cast<size_t>(src_row_stride) ==
                         width * sizeof(device::RgbaTupleF16);
  TRACE_EVENT1("xr", "CopyArCoreImage_RGBA16F: memcpy", "fastPath", fast_path);

  // If they have the same layout, we can copy the entire buffer at once
  if (fast_path) {
    DCHECK_EQ(out_pixels->size() * sizeof(device::RgbaTupleF16),
              static_cast<size_t>(src_buffer_length));
    memcpy(out_pixels->data(), src_buffer, src_buffer_length);
    return;
  }

  // Slow path: copy pixel by pixel, row by row
  for (int32_t row = 0; row < height; ++row) {
    auto* src = src_buffer + src_row_stride * row;
    auto* dest = out_pixels->data() + width * row;

    // For each pixel
    for (int32_t x = 0; x < width; ++x) {
      memcpy(dest, src, sizeof(device::RgbaTupleF16));

      src += src_pixel_stride;
      dest += 1;
    }
  }
}

device::mojom::XRLightProbePtr GetLightProbe(
    ArSession* arcore_session,
    ArLightEstimate* arcore_light_estimate) {
  // ArCore hands out 9 sets of RGB spherical harmonics coefficients
  // https://developers.google.com/ar/reference/c/group/light#arlightestimate_getenvironmentalhdrambientsphericalharmonics
  constexpr size_t kNumShCoefficients = 9;

  auto light_probe = device::mojom::XRLightProbe::New();

  light_probe->spherical_harmonics = device::mojom::XRSphericalHarmonics::New();
  light_probe->spherical_harmonics->coefficients =
      std::vector<device::RgbTupleF32>(kNumShCoefficients,
                                       device::RgbTupleF32{});

  ArLightEstimate_getEnvironmentalHdrAmbientSphericalHarmonics(
      arcore_session, arcore_light_estimate,
      light_probe->spherical_harmonics->coefficients.data()->components);

  float main_light_direction[3] = {0};
  ArLightEstimate_getEnvironmentalHdrMainLightDirection(
      arcore_session, arcore_light_estimate, main_light_direction);
  light_probe->main_light_direction.set_x(main_light_direction[0]);
  light_probe->main_light_direction.set_y(main_light_direction[1]);
  light_probe->main_light_direction.set_z(main_light_direction[2]);

  ArLightEstimate_getEnvironmentalHdrMainLightIntensity(
      arcore_session, arcore_light_estimate,
      light_probe->main_light_intensity.components);

  return light_probe;
}

device::mojom::XRReflectionProbePtr GetReflectionProbe(
    ArSession* arcore_session,
    ArLightEstimate* arcore_light_estimate) {
  ArImageCubemap arcore_cube_map = {nullptr};
  ArLightEstimate_acquireEnvironmentalHdrCubemap(
      arcore_session, arcore_light_estimate, arcore_cube_map);

  auto cube_map = device::mojom::XRCubeMap::New();
  std::vector<device::RgbaTupleF16>* const cube_map_faces[] = {
      &cube_map->positive_x, &cube_map->negative_x, &cube_map->positive_y,
      &cube_map->negative_y, &cube_map->positive_z, &cube_map->negative_z};

  static_assert(
      base::size(cube_map_faces) == base::size(arcore_cube_map),
      "`ArImageCubemap` and `device::mojom::XRCubeMap` are expected to "
      "have the same number of faces (6).");

  static_assert(device::mojom::XRCubeMap::kNumComponentsPerPixel == 4,
                "`device::mojom::XRCubeMap::kNumComponentsPerPixel` is "
                "expected to be 4 (RGBA)`, as that's the format ArCore uses.");

  for (size_t i = 0; i < base::size(arcore_cube_map); ++i) {
    auto* arcore_cube_map_face = arcore_cube_map[i];
    if (!arcore_cube_map_face) {
      DVLOG(1) << "`ArLightEstimate_acquireEnvironmentalHdrCubemap` failed to "
                  "return all faces";
      ReleaseArCoreCubemap(&arcore_cube_map);
      return nullptr;
    }

    auto* cube_map_face = cube_map_faces[i];

    // Make sure we only have a single image plane
    int32_t num_planes = 0;
    ArImage_getNumberOfPlanes(arcore_session, arcore_cube_map_face,
                              &num_planes);
    if (num_planes != 1) {
      DVLOG(1) << "ArCore cube map face " << i
               << " does not have exactly 1 plane.";
      ReleaseArCoreCubemap(&arcore_cube_map);
      return nullptr;
    }

    // Make sure the format for the image is in RGBA16F
    ArImageFormat format = AR_IMAGE_FORMAT_INVALID;
    ArImage_getFormat(arcore_session, arcore_cube_map_face, &format);
    if (format != AR_IMAGE_FORMAT_RGBA_FP16) {
      DVLOG(1) << "ArCore cube map face " << i
               << " not in expected image format.";
      ReleaseArCoreCubemap(&arcore_cube_map);
      return nullptr;
    }

    // Copy the cubemap
    uint32_t face_width = 0, face_height = 0;
    CopyArCoreImage_RGBA16F(arcore_session, arcore_cube_map_face, 0,
                            cube_map_face, &face_width, &face_height);

    // Make sure the cube map is square
    if (face_width != face_height) {
      DVLOG(1) << "ArCore cube map contains non-square image.";
      ReleaseArCoreCubemap(&arcore_cube_map);
      return nullptr;
    }

    // Make sure all faces have the same dimensions
    if (i == 0) {
      cube_map->width_and_height = face_width;
    } else if (face_width != cube_map->width_and_height ||
               face_height != cube_map->width_and_height) {
      DVLOG(1) << "ArCore cube map faces not all of the same dimensions.";
      ReleaseArCoreCubemap(&arcore_cube_map);
      return nullptr;
    }
  }

  ReleaseArCoreCubemap(&arcore_cube_map);

  auto reflection_probe = device::mojom::XRReflectionProbe::New();
  reflection_probe->cube_map = std::move(cube_map);
  return reflection_probe;
}

constexpr float kDefaultFloorHeightEstimation = 1.2;

}  // namespace

namespace device {

HitTestSubscriptionData::HitTestSubscriptionData(
    mojom::XRNativeOriginInformationPtr native_origin_information,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    mojom::XRRayPtr ray)
    : native_origin_information(std::move(native_origin_information)),
      entity_types(entity_types),
      ray(std::move(ray)) {}

HitTestSubscriptionData::HitTestSubscriptionData(
    HitTestSubscriptionData&& other) = default;
HitTestSubscriptionData::~HitTestSubscriptionData() = default;

TransientInputHitTestSubscriptionData::TransientInputHitTestSubscriptionData(
    const std::string& profile_name,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    mojom::XRRayPtr ray)
    : profile_name(profile_name),
      entity_types(entity_types),
      ray(std::move(ray)) {}

TransientInputHitTestSubscriptionData::TransientInputHitTestSubscriptionData(
    TransientInputHitTestSubscriptionData&& other) = default;
TransientInputHitTestSubscriptionData::
    ~TransientInputHitTestSubscriptionData() = default;

ArCoreImpl::ArCoreImpl()
    : gl_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

ArCoreImpl::~ArCoreImpl() = default;

bool ArCoreImpl::Initialize(
    base::android::ScopedJavaLocalRef<jobject> context) {
  DCHECK(IsOnGlThread());
  DCHECK(!arcore_session_.is_valid());

  // TODO(https://crbug.com/837944): Notify error earlier if this will fail.

  JNIEnv* env = base::android::AttachCurrentThread();
  if (!env) {
    DLOG(ERROR) << "Unable to get JNIEnv for ArCore";
    return false;
  }

  // Use a local scoped ArSession for the next steps, we want the
  // arcore_session_ member to remain null until we complete successful
  // initialization.
  internal::ScopedArCoreObject<ArSession*> session;

  ArStatus status = ArSession_create(
      env, context.obj(),
      internal::ScopedArCoreObject<ArSession*>::Receiver(session).get());
  if (status != AR_SUCCESS) {
    DLOG(ERROR) << "ArSession_create failed: " << status;
    return false;
  }

  // Set incognito mode for ARCore session - this is done unconditionally as we
  // always want to limit the amount of logging done by ARCore.
  ArSession_enableIncognitoMode_private(session.get());
  DVLOG(1) << __func__ << ": ARCore incognito mode enabled";

  internal::ScopedArCoreObject<ArConfig*> arcore_config;
  ArConfig_create(
      session.get(),
      internal::ScopedArCoreObject<ArConfig*>::Receiver(arcore_config).get());
  if (!arcore_config.is_valid()) {
    DLOG(ERROR) << "ArConfig_create failed";
    return false;
  }

  // Enable lighting estimation with spherical harmonics
  ArConfig_setLightEstimationMode(session.get(), arcore_config.get(),
                                  AR_LIGHT_ESTIMATION_MODE_ENVIRONMENTAL_HDR);

  status = ArSession_configure(session.get(), arcore_config.get());
  if (status != AR_SUCCESS) {
    DLOG(ERROR) << "ArSession_configure failed: " << status;
    return false;
  }

  internal::ScopedArCoreObject<ArFrame*> frame;
  ArFrame_create(session.get(),
                 internal::ScopedArCoreObject<ArFrame*>::Receiver(frame).get());
  if (!frame.is_valid()) {
    DLOG(ERROR) << "ArFrame_create failed";
    return false;
  }

  internal::ScopedArCoreObject<ArLightEstimate*> light_estimate;
  ArLightEstimate_create(
      session.get(),
      internal::ScopedArCoreObject<ArLightEstimate*>::Receiver(light_estimate)
          .get());
  if (!light_estimate.is_valid()) {
    DVLOG(1) << "ArLightEstimate_create failed";
    return false;
  }

  // Success, we now have a valid session and a valid frame.
  arcore_frame_ = std::move(frame);
  arcore_session_ = std::move(session);
  arcore_light_estimate_ = std::move(light_estimate);
  plane_manager_ = std::make_unique<ArCorePlaneManager>(
      util::PassKey<ArCoreImpl>(), arcore_session_.get());
  return true;
}

void ArCoreImpl::SetCameraTexture(GLuint camera_texture_id) {
  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());
  ArSession_setCameraTextureName(arcore_session_.get(), camera_texture_id);
}

void ArCoreImpl::SetDisplayGeometry(
    const gfx::Size& frame_size,
    display::Display::Rotation display_rotation) {
  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());
  // Display::Rotation is the same as Android's rotation and is compatible with
  // what ArCore is expecting.
  ArSession_setDisplayGeometry(arcore_session_.get(), display_rotation,
                               frame_size.width(), frame_size.height());
}

std::vector<float> ArCoreImpl::TransformDisplayUvCoords(
    const base::span<const float> uvs) {
  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());
  DCHECK(arcore_frame_.is_valid());

  size_t num_elements = uvs.size();
  DCHECK(num_elements % 2 == 0);
  std::vector<float> uvs_out(num_elements);

  ArFrame_transformCoordinates2d(
      arcore_session_.get(), arcore_frame_.get(),
      AR_COORDINATES_2D_VIEW_NORMALIZED, num_elements / 2, &uvs[0],
      AR_COORDINATES_2D_TEXTURE_NORMALIZED, &uvs_out[0]);
  return uvs_out;
}

mojom::VRPosePtr ArCoreImpl::Update(bool* camera_updated) {
  TRACE_EVENT0("gpu", "ArCoreImpl Update");

  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());
  DCHECK(arcore_frame_.is_valid());
  DCHECK(camera_updated);

  ArStatus status;

  TRACE_EVENT_BEGIN0("gpu", "ArCore Update");
  status = ArSession_update(arcore_session_.get(), arcore_frame_.get());
  TRACE_EVENT_END0("gpu", "ArCore Update");

  if (status != AR_SUCCESS) {
    DLOG(ERROR) << "ArSession_update failed: " << status;
    *camera_updated = false;
    return nullptr;
  }

  // If we get here, assume we have a valid camera image, but we don't know yet
  // if tracking is working.
  *camera_updated = true;
  internal::ScopedArCoreObject<ArCamera*> arcore_camera;
  ArFrame_acquireCamera(
      arcore_session_.get(), arcore_frame_.get(),
      internal::ScopedArCoreObject<ArCamera*>::Receiver(arcore_camera).get());
  if (!arcore_camera.is_valid()) {
    DLOG(ERROR) << "ArFrame_acquireCamera failed!";
    return nullptr;
  }

  ArTrackingState tracking_state;
  ArCamera_getTrackingState(arcore_session_.get(), arcore_camera.get(),
                            &tracking_state);
  if (tracking_state != AR_TRACKING_STATE_TRACKING) {
    DVLOG(1) << "Tracking state is not AR_TRACKING_STATE_TRACKING: "
             << tracking_state;
    return nullptr;
  }

  internal::ScopedArCoreObject<ArPose*> arcore_pose;
  ArPose_create(
      arcore_session_.get(), nullptr,
      internal::ScopedArCoreObject<ArPose*>::Receiver(arcore_pose).get());
  if (!arcore_pose.is_valid()) {
    DLOG(ERROR) << "ArPose_create failed!";
    return nullptr;
  }

  ArCamera_getDisplayOrientedPose(arcore_session_.get(), arcore_camera.get(),
                                  arcore_pose.get());

  TRACE_EVENT_BEGIN0("gpu", "ArCorePlaneManager Update");
  plane_manager_->Update(arcore_frame_.get());
  TRACE_EVENT_END0("gpu", "ArCorePlaneManager Update");

  return GetMojomVRPoseFromArPose(arcore_session_.get(), arcore_pose.get());
}

void ArCoreImpl::EnsureArCoreAnchorsList() {
  if (!arcore_anchors_.is_valid()) {
    ArAnchorList_create(
        arcore_session_.get(),
        internal::ScopedArCoreObject<ArAnchorList*>::Receiver(arcore_anchors_)
            .get());
    DCHECK(arcore_anchors_.is_valid());
  }
}

std::vector<mojom::XRAnchorDataPtr> ArCoreImpl::GetUpdatedAnchorsData() {
  EnsureArCoreAnchorsList();

  std::vector<mojom::XRAnchorDataPtr> result;

  ArFrame_getUpdatedAnchors(arcore_session_.get(), arcore_frame_.get(),
                            arcore_anchors_.get());

  ForEachArCoreAnchor([this, &result](ArAnchor* ar_anchor) {
    // pose
    internal::ScopedArCoreObject<ArPose*> anchor_pose;
    ArPose_create(
        arcore_session_.get(), nullptr,
        internal::ScopedArCoreObject<ArPose*>::Receiver(anchor_pose).get());
    ArAnchor_getPose(arcore_session_.get(), ar_anchor, anchor_pose.get());
    mojom::Pose pose =
        GetMojomPoseFromArPose(arcore_session_.get(), anchor_pose.get());

    // ID
    AnchorId anchor_id;
    bool created;
    std::tie(anchor_id, created) = CreateOrGetAnchorId(ar_anchor);

    DCHECK(!created)
        << "Anchor creation is app-initiated - we should never encounter an "
           "anchor that was created outside of `ArCoreImpl::CreateAnchor()`.";

    result.push_back(mojom::XRAnchorData::New(anchor_id.GetUnsafeValue(),
                                              device::mojom::Pose::New(pose)));
  });

  return result;
}

std::vector<uint64_t> ArCoreImpl::GetAllAnchorIds() {
  EnsureArCoreAnchorsList();

  std::vector<uint64_t> result;

  ArSession_getAllAnchors(arcore_session_.get(), arcore_anchors_.get());

  ForEachArCoreAnchor([this, &result](ArAnchor* ar_anchor) {
    // ID
    AnchorId anchor_id;
    bool created;
    std::tie(anchor_id, created) = CreateOrGetAnchorId(ar_anchor);

    DCHECK(!created)
        << "Anchor creation is app-initiated - we should never encounter an "
           "anchor that was created outside of `ArCoreImpl::CreateAnchor()`.";

    result.emplace_back(anchor_id);
  });

  return result;
}

template <typename FunctionType>
void ArCoreImpl::ForEachArCoreAnchor(FunctionType fn) {
  DCHECK(arcore_anchors_.is_valid());

  int32_t anchor_list_size;
  ArAnchorList_getSize(arcore_session_.get(), arcore_anchors_.get(),
                       &anchor_list_size);

  for (int i = 0; i < anchor_list_size; i++) {
    internal::ScopedArCoreObject<ArAnchor*> anchor;
    ArAnchorList_acquireItem(
        arcore_session_.get(), arcore_anchors_.get(), i,
        internal::ScopedArCoreObject<ArAnchor*>::Receiver(anchor).get());

    ArTrackingState tracking_state;
    ArAnchor_getTrackingState(arcore_session_.get(), anchor.get(),
                              &tracking_state);

    if (tracking_state != ArTrackingState::AR_TRACKING_STATE_TRACKING) {
      // Skip all anchors that are not currently tracked.
      continue;
    }

    fn(anchor.get());
  }
}

mojom::XRPlaneDetectionDataPtr ArCoreImpl::GetDetectedPlanesData() {
  DVLOG(2) << __func__;

  TRACE_EVENT0("gpu", __func__);

  return plane_manager_->GetDetectedPlanesData();
}

mojom::XRAnchorsDataPtr ArCoreImpl::GetAnchorsData() {
  TRACE_EVENT0("gpu", __func__);

  auto updated_anchors = GetUpdatedAnchorsData();
  auto all_anchor_ids = GetAllAnchorIds();

  return mojom::XRAnchorsData::New(all_anchor_ids, std::move(updated_anchors));
}

mojom::XRLightEstimationDataPtr ArCoreImpl::GetLightEstimationData() {
  TRACE_EVENT0("gpu", __func__);

  ArFrame_getLightEstimate(arcore_session_.get(), arcore_frame_.get(),
                           arcore_light_estimate_.get());

  ArLightEstimateState light_estimate_state = AR_LIGHT_ESTIMATE_STATE_NOT_VALID;
  ArLightEstimate_getState(arcore_session_.get(), arcore_light_estimate_.get(),
                           &light_estimate_state);

  // The light estimate state is not guaranteed to be valid initially
  if (light_estimate_state != AR_LIGHT_ESTIMATE_STATE_VALID) {
    DVLOG(2) << "ArCore light estimation state invalid.";
    return nullptr;
  }

  auto light_estimation_data = mojom::XRLightEstimationData::New();
  light_estimation_data->light_probe =
      GetLightProbe(arcore_session_.get(), arcore_light_estimate_.get());
  if (!light_estimation_data->light_probe) {
    DVLOG(1) << "Failed to generate light probe.";
    return nullptr;
  }
  light_estimation_data->reflection_probe =
      GetReflectionProbe(arcore_session_.get(), arcore_light_estimate_.get());
  if (!light_estimation_data->reflection_probe) {
    DVLOG(1) << "Failed to generate reflection probe.";
    return nullptr;
  }

  return light_estimation_data;
}

std::pair<AnchorId, bool> ArCoreImpl::CreateOrGetAnchorId(
    void* anchor_address) {
  auto it = ar_anchor_address_to_id_.find(anchor_address);
  if (it != ar_anchor_address_to_id_.end()) {
    return std::make_pair(it->second, false);
  }

  CHECK(next_id_ != std::numeric_limits<uint64_t>::max())
      << "preventing ID overflow";

  uint64_t current_id = next_id_++;
  ar_anchor_address_to_id_.emplace(anchor_address, current_id);

  return std::make_pair(AnchorId(current_id), true);
}

void ArCoreImpl::Pause() {
  DVLOG(2) << __func__;

  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());

  ArStatus status = ArSession_pause(arcore_session_.get());
  DLOG_IF(ERROR, status != AR_SUCCESS)
      << "ArSession_pause failed: status = " << status;
}

void ArCoreImpl::Resume() {
  DVLOG(2) << __func__;

  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());

  ArStatus status = ArSession_resume(arcore_session_.get());
  DLOG_IF(ERROR, status != AR_SUCCESS)
      << "ArSession_resume failed: status = " << status;
}

gfx::Transform ArCoreImpl::GetProjectionMatrix(float near, float far) {
  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());
  DCHECK(arcore_frame_.is_valid());

  internal::ScopedArCoreObject<ArCamera*> arcore_camera;
  ArFrame_acquireCamera(
      arcore_session_.get(), arcore_frame_.get(),
      internal::ScopedArCoreObject<ArCamera*>::Receiver(arcore_camera).get());
  DCHECK(arcore_camera.is_valid())
      << "ArFrame_acquireCamera failed despite documentation saying it cannot";

  // ArCore's projection matrix is 16 floats in column-major order.
  float matrix_4x4[16];
  ArCamera_getProjectionMatrix(arcore_session_.get(), arcore_camera.get(), near,
                               far, matrix_4x4);
  gfx::Transform result;
  result.matrix().setColMajorf(matrix_4x4);
  return result;
}

float ArCoreImpl::GetEstimatedFloorHeight() {
  return kDefaultFloorHeightEstimation;
}

base::Optional<uint64_t> ArCoreImpl::SubscribeToHitTest(
    mojom::XRNativeOriginInformationPtr native_origin_information,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    mojom::XRRayPtr ray) {
  // First, check if we recognize the type of the native origin.

  if (native_origin_information->is_reference_space_category()) {
    // Reference spaces are implicitly recognized and don't carry an ID.
  } else if (native_origin_information->is_input_source_id()) {
    // Input source IDs are verified in the higher layer as ArCoreImpl does
    // not carry input source state.
  } else if (native_origin_information->is_plane_id()) {
    // Validate that we know which plane's space the hit test is interested in
    // tracking.
    if (!plane_manager_->PlaneExists(
            PlaneId(native_origin_information->get_plane_id()))) {
      return base::nullopt;
    }
  } else if (native_origin_information->is_anchor_id()) {
    // Validate that we know which anchor's space the hit test is interested
    // in tracking.
    if (anchor_id_to_anchor_object_.count(
            AnchorId(native_origin_information->get_anchor_id())) == 0) {
      return base::nullopt;
    }
  } else {
    NOTREACHED();
    return base::nullopt;
  }

  auto subscription_id = CreateHitTestSubscriptionId();

  hit_test_subscription_id_to_data_.emplace(
      subscription_id,
      HitTestSubscriptionData{std::move(native_origin_information),
                              entity_types, std::move(ray)});

  return subscription_id.GetUnsafeValue();
}

base::Optional<uint64_t> ArCoreImpl::SubscribeToHitTestForTransientInput(
    const std::string& profile_name,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    mojom::XRRayPtr ray) {
  auto subscription_id = CreateHitTestSubscriptionId();

  hit_test_subscription_id_to_transient_hit_test_data_.emplace(
      subscription_id, TransientInputHitTestSubscriptionData{
                           profile_name, entity_types, std::move(ray)});

  return subscription_id.GetUnsafeValue();
}

mojom::XRHitTestSubscriptionResultsDataPtr
ArCoreImpl::GetHitTestSubscriptionResults(
    const gfx::Transform& mojo_from_viewer,
    const base::Optional<std::vector<mojom::XRInputSourceStatePtr>>&
        maybe_input_state) {
  mojom::XRHitTestSubscriptionResultsDataPtr result =
      mojom::XRHitTestSubscriptionResultsData::New();

  for (auto& subscription_id_and_data : hit_test_subscription_id_to_data_) {
    // First, check if we can find the current transformation for a ray. If not,
    // skip processing this subscription.
    auto maybe_mojo_from_native_origin = GetMojoFromNativeOrigin(
        subscription_id_and_data.second.native_origin_information,
        mojo_from_viewer, maybe_input_state);

    if (!maybe_mojo_from_native_origin) {
      continue;
    }

    // Since we have a transform, let's use it to obtain hit test results.
    result->results.push_back(GetHitTestSubscriptionResult(
        HitTestSubscriptionId(subscription_id_and_data.first),
        *subscription_id_and_data.second.ray,
        subscription_id_and_data.second.entity_types,
        *maybe_mojo_from_native_origin));
  }

  for (const auto& subscribtion_id_and_data :
       hit_test_subscription_id_to_transient_hit_test_data_) {
    auto input_source_ids_and_transforms =
        GetMojoFromInputSources(subscribtion_id_and_data.second.profile_name,
                                mojo_from_viewer, maybe_input_state);

    result->transient_input_results.push_back(
        GetTransientHitTestSubscriptionResult(
            HitTestSubscriptionId(subscribtion_id_and_data.first),
            *subscribtion_id_and_data.second.ray,
            subscribtion_id_and_data.second.entity_types,
            input_source_ids_and_transforms));
  }

  return result;
}

device::mojom::XRHitTestSubscriptionResultDataPtr
ArCoreImpl::GetHitTestSubscriptionResult(
    HitTestSubscriptionId id,
    const mojom::XRRay& native_origin_ray,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    const gfx::Transform& mojo_from_native_origin) {
  // Transform the ray according to the latest transform based on the XRSpace
  // used in hit test subscription.

  gfx::Point3F origin = native_origin_ray.origin;
  mojo_from_native_origin.TransformPoint(&origin);

  gfx::Vector3dF direction = native_origin_ray.direction;
  mojo_from_native_origin.TransformVector(&direction);

  std::vector<mojom::XRHitResultPtr> hit_results;
  if (!RequestHitTest(origin, direction, entity_types, &hit_results)) {
    hit_results.clear();  // On failure, clear partial results.
  }

  return mojom::XRHitTestSubscriptionResultData::New(id.GetUnsafeValue(),
                                                     std::move(hit_results));
}

device::mojom::XRHitTestTransientInputSubscriptionResultDataPtr
ArCoreImpl::GetTransientHitTestSubscriptionResult(
    HitTestSubscriptionId id,
    const mojom::XRRay& input_source_ray,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    const std::vector<std::pair<uint32_t, gfx::Transform>>&
        input_source_ids_and_mojo_from_input_sources) {
  auto result =
      device::mojom::XRHitTestTransientInputSubscriptionResultData::New();

  result->subscription_id = id.GetUnsafeValue();

  for (const auto& input_source_id_and_mojo_from_input_source :
       input_source_ids_and_mojo_from_input_sources) {
    gfx::Point3F origin = input_source_ray.origin;
    input_source_id_and_mojo_from_input_source.second.TransformPoint(&origin);

    gfx::Vector3dF direction = input_source_ray.direction;
    input_source_id_and_mojo_from_input_source.second.TransformVector(
        &direction);

    std::vector<mojom::XRHitResultPtr> hit_results;
    if (!RequestHitTest(origin, direction, entity_types, &hit_results)) {
      hit_results.clear();  // On failure, clear partial results.
    }

    result->input_source_id_to_hit_test_results.insert(
        {input_source_id_and_mojo_from_input_source.first,
         std::move(hit_results)});
  }

  return result;
}

std::vector<std::pair<uint32_t, gfx::Transform>>
ArCoreImpl::GetMojoFromInputSources(
    const std::string& profile_name,
    const gfx::Transform& mojo_from_viewer,
    const base::Optional<std::vector<mojom::XRInputSourceStatePtr>>&
        maybe_input_state) {
  std::vector<std::pair<uint32_t, gfx::Transform>> result;

  if (!maybe_input_state) {
    return result;
  }

  for (const auto& input_state : *maybe_input_state) {
    if (input_state && input_state->description) {
      if (base::Contains(input_state->description->profiles, profile_name)) {
        // Input source represented by input_state matches the profile, find
        // the transform and grab input source id.
        base::Optional<gfx::Transform> maybe_mojo_from_input_source =
            GetMojoFromInputSource(input_state, mojo_from_viewer);

        if (!maybe_mojo_from_input_source)
          continue;

        result.push_back(
            {input_state->source_id, *maybe_mojo_from_input_source});
      }
    }
  }

  return result;
}

base::Optional<gfx::Transform> ArCoreImpl::GetMojoFromReferenceSpace(
    device::mojom::XRReferenceSpaceCategory category,
    const gfx::Transform& mojo_from_viewer) {
  switch (category) {
    case device::mojom::XRReferenceSpaceCategory::LOCAL:
      return gfx::Transform{};
    case device::mojom::XRReferenceSpaceCategory::LOCAL_FLOOR: {
      auto result = gfx::Transform{};
      result.Translate3d(0, -GetEstimatedFloorHeight(), 0);
      return result;
    }
    case device::mojom::XRReferenceSpaceCategory::VIEWER:
      return mojo_from_viewer;
    case device::mojom::XRReferenceSpaceCategory::BOUNDED_FLOOR:
      return base::nullopt;
    case device::mojom::XRReferenceSpaceCategory::UNBOUNDED:
      return base::nullopt;
  }
}

base::Optional<gfx::Transform> ArCoreImpl::GetMojoFromNativeOrigin(
    const mojom::XRNativeOriginInformationPtr& native_origin_information,
    const gfx::Transform& mojo_from_viewer,
    const base::Optional<std::vector<mojom::XRInputSourceStatePtr>>&
        maybe_input_state) {
  if (native_origin_information->is_input_source_id()) {
    if (!maybe_input_state) {
      return base::nullopt;
    }

    // Linear search should be fine for ARCore device as it only has one input
    // source (for now).
    for (auto& input_source_state : *maybe_input_state) {
      if (input_source_state->source_id ==
          native_origin_information->get_input_source_id()) {
        return GetMojoFromInputSource(input_source_state, mojo_from_viewer);
      }
    }

    return base::nullopt;
  } else if (native_origin_information->is_reference_space_category()) {
    return GetMojoFromReferenceSpace(
        native_origin_information->get_reference_space_category(),
        mojo_from_viewer);
  } else if (native_origin_information->is_plane_id()) {
    return plane_manager_->GetMojoFromPlane(
        PlaneId(native_origin_information->get_plane_id()));
  } else if (native_origin_information->is_anchor_id()) {
    auto anchor_it = anchor_id_to_anchor_object_.find(
        AnchorId(native_origin_information->get_anchor_id()));
    if (anchor_it == anchor_id_to_anchor_object_.end()) {
      return base::nullopt;
    }

    internal::ScopedArCoreObject<ArPose*> ar_pose;
    ArPose_create(
        arcore_session_.get(), nullptr,
        internal::ScopedArCoreObject<ArPose*>::Receiver(ar_pose).get());

    ArAnchor_getPose(arcore_session_.get(), anchor_it->second.get(),
                     ar_pose.get());
    mojom::Pose mojo_pose =
        GetMojomPoseFromArPose(arcore_session_.get(), ar_pose.get());

    return mojo::ConvertTo<gfx::Transform>(mojo_pose);
  } else {
    NOTREACHED();
    return base::nullopt;
  }
}  // namespace device

void ArCoreImpl::UnsubscribeFromHitTest(uint64_t subscription_id) {
  auto it = hit_test_subscription_id_to_data_.find(
      HitTestSubscriptionId(subscription_id));
  if (it == hit_test_subscription_id_to_data_.end()) {
    return;
  }

  hit_test_subscription_id_to_data_.erase(it);
}

HitTestSubscriptionId ArCoreImpl::CreateHitTestSubscriptionId() {
  CHECK(next_id_ != std::numeric_limits<uint64_t>::max())
      << "preventing ID overflow";

  uint64_t current_id = next_id_++;

  return HitTestSubscriptionId(current_id);
}

bool ArCoreImpl::RequestHitTest(
    const mojom::XRRayPtr& ray,
    std::vector<mojom::XRHitResultPtr>* hit_results) {
  DCHECK(ray);
  return RequestHitTest(ray->origin, ray->direction,
                        {mojom::EntityTypeForHitTest::PLANE},
                        hit_results);  // "Plane" to maintain current behavior
                                       // of async hit test.
}

bool ArCoreImpl::RequestHitTest(
    const gfx::Point3F& origin,
    const gfx::Vector3dF& direction,
    const std::vector<mojom::EntityTypeForHitTest>& entity_types,
    std::vector<mojom::XRHitResultPtr>* hit_results) {
  DVLOG(2) << __func__ << ": origin=" << origin.ToString()
           << ", direction=" << direction.ToString();

  DCHECK(hit_results);
  DCHECK(IsOnGlThread());
  DCHECK(arcore_session_.is_valid());
  DCHECK(arcore_frame_.is_valid());

  auto arcore_entity_types = GetArCoreEntityTypes(entity_types);

  // ArCore returns hit-results in sorted order, thus providing the guarantee
  // of sorted results promised by the WebXR spec for requestHitTest().
  std::array<float, 3> origin_array = {origin.x(), origin.y(), origin.z()};
  std::array<float, 3> direction_array = {direction.x(), direction.y(),
                                          direction.z()};

  internal::ScopedArCoreObject<ArHitResultList*> arcore_hit_result_list;
  ArHitResultList_create(
      arcore_session_.get(),
      internal::ScopedArCoreObject<ArHitResultList*>::Receiver(
          arcore_hit_result_list)
          .get());
  if (!arcore_hit_result_list.is_valid()) {
    DLOG(ERROR) << "ArHitResultList_create failed!";
    return false;
  }

  ArFrame_hitTestRay(arcore_session_.get(), arcore_frame_.get(),
                     origin_array.data(), direction_array.data(),
                     arcore_hit_result_list.get());

  int arcore_hit_result_list_size = 0;
  ArHitResultList_getSize(arcore_session_.get(), arcore_hit_result_list.get(),
                          &arcore_hit_result_list_size);
  DVLOG(2) << __func__
           << ": arcore_hit_result_list_size=" << arcore_hit_result_list_size;

  // Go through the list in reverse order so the first hit we encounter is the
  // furthest.
  // We will accept the furthest hit and then for the rest require that the hit
  // be within the actual polygon detected by ArCore. This heuristic allows us
  // to get better results on floors w/o overestimating the size of tables etc.
  // See https://crbug.com/872855.
  for (int i = arcore_hit_result_list_size - 1; i >= 0; i--) {
    internal::ScopedArCoreObject<ArHitResult*> arcore_hit;

    ArHitResult_create(
        arcore_session_.get(),
        internal::ScopedArCoreObject<ArHitResult*>::Receiver(arcore_hit).get());

    if (!arcore_hit.is_valid()) {
      DLOG(ERROR) << "ArHitResult_create failed!";
      return false;
    }

    ArHitResultList_getItem(arcore_session_.get(), arcore_hit_result_list.get(),
                            i, arcore_hit.get());

    internal::ScopedArCoreObject<ArTrackable*> ar_trackable;

    ArHitResult_acquireTrackable(
        arcore_session_.get(), arcore_hit.get(),
        internal::ScopedArCoreObject<ArTrackable*>::Receiver(ar_trackable)
            .get());
    ArTrackableType ar_trackable_type = AR_TRACKABLE_NOT_VALID;
    ArTrackable_getType(arcore_session_.get(), ar_trackable.get(),
                        &ar_trackable_type);

    // Only consider trackables listed in arcore_entity_types.
    if (!base::Contains(arcore_entity_types, ar_trackable_type)) {
      DVLOG(2)
          << __func__
          << ": hit a trackable that is not in entity types set, ignoring it";
      continue;
    }

    internal::ScopedArCoreObject<ArPose*> arcore_pose;
    ArPose_create(
        arcore_session_.get(), nullptr,
        internal::ScopedArCoreObject<ArPose*>::Receiver(arcore_pose).get());
    if (!arcore_pose.is_valid()) {
      DLOG(ERROR) << "ArPose_create failed!";
      return false;
    }

    ArHitResult_getHitPose(arcore_session_.get(), arcore_hit.get(),
                           arcore_pose.get());

    // After the first (furthest) hit, for planes, only return hits that are
    // within the actual detected polygon and not just within than the larger
    // plane.
    uint64_t plane_id = 0;
    if (!hit_results->empty() && ar_trackable_type == AR_TRACKABLE_PLANE) {
      int32_t in_polygon = 0;
      ArPlane* ar_plane = ArAsPlane(ar_trackable.get());
      ArPlane_isPoseInPolygon(arcore_session_.get(), ar_plane,
                              arcore_pose.get(), &in_polygon);
      if (!in_polygon) {
        DVLOG(2) << __func__
                 << ": hit a trackable that is not within detected polygon, "
                    "ignoring it";
        continue;
      }

      base::Optional<PlaneId> maybe_plane_id =
          plane_manager_->GetPlaneId(ar_plane);
      if (maybe_plane_id) {
        plane_id = maybe_plane_id->GetUnsafeValue();
      }
    }

    std::array<float, 16> matrix;
    ArPose_getMatrix(arcore_session_.get(), arcore_pose.get(), matrix.data());

    mojom::XRHitResultPtr mojo_hit = mojom::XRHitResult::New();

    // ArPose_getMatrix returns the matrix in WebGL style column-major order
    // and gfx::Transform expects row major order.
    // clang-format off
    mojo_hit->hit_matrix = gfx::Transform(
      matrix[0], matrix[4], matrix[8],  matrix[12],
      matrix[1], matrix[5], matrix[9],  matrix[13],
      matrix[2], matrix[6], matrix[10], matrix[14],
      matrix[3], matrix[7], matrix[11], matrix[15]
    );
    // clang-format on

    mojo_hit->plane_id = plane_id;

    // Insert new results at head to preserver order from ArCore
    hit_results->insert(hit_results->begin(), std::move(mojo_hit));
  }

  DVLOG(2) << __func__ << ": hit_results->size()=" << hit_results->size();
  return true;
}

base::Optional<uint64_t> ArCoreImpl::CreateAnchor(
    const device::mojom::Pose& pose) {
  auto ar_pose = GetArPoseFromMojomPose(arcore_session_.get(), pose);

  device::internal::ScopedArCoreObject<ArAnchor*> ar_anchor;
  ArStatus status = ArSession_acquireNewAnchor(
      arcore_session_.get(), ar_pose.get(),
      device::internal::ScopedArCoreObject<ArAnchor*>::Receiver(ar_anchor)
          .get());

  if (status != AR_SUCCESS) {
    return base::nullopt;
  }

  AnchorId anchor_id;
  bool created;
  std::tie(anchor_id, created) = CreateOrGetAnchorId(ar_anchor.get());

  DCHECK(created) << "This should always be a new anchor, not something we've "
                     "seen previously.";

  anchor_id_to_anchor_object_[anchor_id] = std::move(ar_anchor);

  return anchor_id.GetUnsafeValue();
}

base::Optional<uint64_t> ArCoreImpl::CreateAnchor(
    const device::mojom::Pose& pose,
    uint64_t plane_id) {
  DVLOG(2) << __func__ << ": plane_id=" << plane_id;

  auto ar_anchor = plane_manager_->CreateAnchor(PlaneId(plane_id), pose);
  if (!ar_anchor.is_valid()) {
    return base::nullopt;
  }

  AnchorId anchor_id;
  bool created;
  std::tie(anchor_id, created) = CreateOrGetAnchorId(ar_anchor.get());

  DCHECK(created) << "This should always be a new anchor, not something we've "
                     "seen previously.";

  anchor_id_to_anchor_object_[anchor_id] = std::move(ar_anchor);

  return anchor_id.GetUnsafeValue();
}

void ArCoreImpl::DetachAnchor(uint64_t anchor_id) {
  auto it = anchor_id_to_anchor_object_.find(AnchorId(anchor_id));
  if (it == anchor_id_to_anchor_object_.end()) {
    return;
  }

  ArAnchor_detach(arcore_session_.get(), it->second.get());

  anchor_id_to_anchor_object_.erase(it);
}

bool ArCoreImpl::IsOnGlThread() {
  return gl_thread_task_runner_->BelongsToCurrentThread();
}

std::unique_ptr<ArCore> ArCoreImplFactory::Create() {
  return std::make_unique<ArCoreImpl>();
}

}  // namespace device
