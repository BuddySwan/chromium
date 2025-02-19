/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_SPEECH_SPEECH_SYNTHESIS_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_SPEECH_SPEECH_SYNTHESIS_H_

#include "third_party/blink/public/mojom/speech/speech_synthesis.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/modules/event_target_modules.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/modules/speech/speech_synthesis_utterance.h"
#include "third_party/blink/renderer/modules/speech/speech_synthesis_voice.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_receiver.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"

namespace blink {

class MODULES_EXPORT SpeechSynthesis final
    : public EventTargetWithInlineData,
      public ExecutionContextClient,
      public mojom::blink::SpeechSynthesisVoiceListObserver {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(SpeechSynthesis);

 public:
  static SpeechSynthesis* Create(ExecutionContext*);
  static SpeechSynthesis* CreateForTesting(
      ExecutionContext*,
      mojo::PendingRemote<mojom::blink::SpeechSynthesis>);

  explicit SpeechSynthesis(ExecutionContext*);

  bool pending() const;
  bool speaking() const;
  bool paused() const;

  void speak(SpeechSynthesisUtterance*);
  void cancel();
  void pause();
  void resume();

  const HeapVector<Member<SpeechSynthesisVoice>>& getVoices();

  DEFINE_ATTRIBUTE_EVENT_LISTENER(voiceschanged, kVoiceschanged)

  ExecutionContext* GetExecutionContext() const override {
    return ExecutionContextClient::GetExecutionContext();
  }

  // GarbageCollected
  void Trace(Visitor*) override;

  // mojom::blink::SpeechSynthesisVoiceListObserver
  void OnSetVoiceList(
      Vector<mojom::blink::SpeechSynthesisVoicePtr> voices) override;

  // These methods are called by SpeechSynthesisUtterance:
  void DidStartSpeaking(SpeechSynthesisUtterance*);
  void DidPauseSpeaking(SpeechSynthesisUtterance*);
  void DidResumeSpeaking(SpeechSynthesisUtterance*);
  void DidFinishSpeaking(SpeechSynthesisUtterance*);
  void SpeakingErrorOccurred(SpeechSynthesisUtterance*);
  void WordBoundaryEventOccurred(SpeechSynthesisUtterance*,
                                 unsigned char_index,
                                 unsigned char_length);
  void SentenceBoundaryEventOccurred(SpeechSynthesisUtterance*,
                                     unsigned char_index,
                                     unsigned char_length);

  mojom::blink::SpeechSynthesis* MojomSynthesis() {
    return mojom_synthesis_.get();
  }

 private:
  void VoicesDidChange();
  void StartSpeakingImmediately();
  void HandleSpeakingCompleted(SpeechSynthesisUtterance*, bool error_occurred);
  void FireEvent(const AtomicString& type,
                 SpeechSynthesisUtterance*,
                 uint32_t char_index,
                 uint32_t char_length,
                 const String& name);

  void FireErrorEvent(SpeechSynthesisUtterance*,
                      uint32_t char_index,
                      const String& error);

  // Returns the utterance at the front of the queue.
  SpeechSynthesisUtterance* CurrentSpeechUtterance() const;

  // Gets a timestamp in millis that is safe to expose to the web.
  // Returns false if it cannot get a timestamp.
  bool GetElapsedTimeMillis(double* millis);

  bool IsAllowedToStartByAutoplay() const;

  void SetMojomSynthesisForTesting(
      mojo::PendingRemote<mojom::blink::SpeechSynthesis>);
  void InitializeMojomSynthesis();
  void InitializeMojomSynthesisIfNeeded();

  HeapMojoReceiver<mojom::blink::SpeechSynthesisVoiceListObserver> receiver_;
  HeapMojoRemote<mojom::blink::SpeechSynthesis> mojom_synthesis_;
  HeapVector<Member<SpeechSynthesisVoice>> voice_list_;
  HeapDeque<Member<SpeechSynthesisUtterance>> utterance_queue_;
  bool is_paused_ = false;

  // EventTarget
  const AtomicString& InterfaceName() const override;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_SPEECH_SPEECH_SYNTHESIS_H_
