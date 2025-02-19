// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/sync/password_data_type_controller.h"

#include "base/bind.h"
#include "base/metrics/histogram.h"
#include "components/password_manager/core/browser/password_store.h"
#include "components/sync/driver/sync_client.h"
#include "components/sync/driver/sync_service.h"

namespace browser_sync {

PasswordDataTypeController::PasswordDataTypeController(
    const base::RepeatingClosure& dump_stack,
    syncer::SyncService* sync_service,
    syncer::SyncClient* sync_client,
    const base::RepeatingClosure& state_changed_callback,
    const scoped_refptr<password_manager::PasswordStore>& password_store)
    : AsyncDirectoryTypeController(syncer::PASSWORDS,
                                   dump_stack,
                                   sync_service,
                                   sync_client,
                                   syncer::GROUP_PASSWORD,
                                   nullptr),
      state_changed_callback_(state_changed_callback),
      password_store_(password_store) {}

PasswordDataTypeController::~PasswordDataTypeController() {}

bool PasswordDataTypeController::PostTaskOnModelThread(
    const base::Location& from_here,
    base::OnceClosure task) {
  DCHECK(CalledOnValidThread());
  if (!password_store_)
    return false;
  return password_store_->ScheduleTask(std::move(task));
}

bool PasswordDataTypeController::StartModels() {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(MODEL_STARTING, state());

  sync_service()->AddObserver(this);

  OnStateChanged(sync_service());

  return !!password_store_;
}

void PasswordDataTypeController::StopModels() {
  DCHECK(CalledOnValidThread());
  sync_service()->RemoveObserver(this);
}

void PasswordDataTypeController::OnStateChanged(syncer::SyncService* sync) {
  DCHECK(CalledOnValidThread());
  state_changed_callback_.Run();
}

}  // namespace browser_sync
