// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_HISTORY_MEDIA_HISTORY_CONTENTS_OBSERVER_H_
#define CHROME_BROWSER_MEDIA_HISTORY_MEDIA_HISTORY_CONTENTS_OBSERVER_H_

#include "base/optional.h"
#include "chrome/browser/media/history/media_history_keyed_service.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "url/gurl.h"

class MediaHistoryContentsObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MediaHistoryContentsObserver>,
      public media_session::mojom::MediaSessionObserver {
 public:
  ~MediaHistoryContentsObserver() override;

  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void MediaWatchTimeChanged(
      const content::MediaPlayerWatchTime& watch_time) override;
  void WebContentsDestroyed() override;

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr session_info) override;
  void MediaSessionMetadataChanged(
      const base::Optional<media_session::MediaMetadata>&) override;
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>& action)
      override {}
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override;
  void MediaSessionPositionChanged(
      const base::Optional<media_session::MediaPosition>& position) override;

 private:
  friend class content::WebContentsUserData<MediaHistoryContentsObserver>;

  explicit MediaHistoryContentsObserver(content::WebContents* web_contents);

  void MaybeCommitMediaSession();

  // Stores the current media session metadata, position, artwork urls and URL
  // that might be committed to media history.
  base::Optional<media_session::MediaMetadata> cached_metadata_;
  base::Optional<media_session::MediaPosition> cached_position_;
  std::vector<media_session::MediaImage> cached_artwork_;
  GURL current_url_;

  // Stores whether the media session on this web contents have ever played.
  bool has_been_active_ = false;

  // If the web contents is currently navigating then we freeze any updates to
  // the media session metadata and position. This is because it is cleared
  // before we can commit it to the database.
  bool frozen_ = false;

  media_history::MediaHistoryKeyedService* service_;

  mojo::Receiver<media_session::mojom::MediaSessionObserver> observer_receiver_{
      this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();

  DISALLOW_COPY_AND_ASSIGN(MediaHistoryContentsObserver);
};

#endif  // CHROME_BROWSER_MEDIA_HISTORY_MEDIA_HISTORY_CONTENTS_OBSERVER_H_
