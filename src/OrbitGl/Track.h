// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_GL_TRACK_H_
#define ORBIT_GL_TRACK_H_

#include <GteVector.h>
#include <OrbitClientModel/CaptureData.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Batcher.h"
#include "BlockChain.h"
#include "CaptureViewElement.h"
#include "CoreMath.h"
#include "OrbitBase/Profiling.h"
#include "TextBox.h"
#include "TextRenderer.h"
#include "TimeGraphLayout.h"
#include "TimerChain.h"
#include "TrackAccessibility.h"
#include "TriangleToggle.h"
#include "capture_data.pb.h"

class Track : public orbit_gl::CaptureViewElement, public std::enable_shared_from_this<Track> {
 public:
  enum Type {
    kTimerTrack,
    kThreadTrack,
    kEventTrack,
    kFrameTrack,
    kGraphTrack,
    kGpuTrack,
    kSchedulerTrack,
    kAsyncTrack,
    kThreadStateTrack,
    kUnknown,
  };

  explicit Track(TimeGraph* time_graph, TimeGraphLayout* layout, const CaptureData* capture_data);
  ~Track() override = default;

  void Draw(GlCanvas* canvas, PickingMode picking_mode, float z_offset = 0) override;

  void UpdatePrimitives(Batcher* batcher, uint64_t min_tick, uint64_t max_tick,
                        PickingMode picking_mode, float z_offset = 0) override;
  void OnDrag(int x, int y) override;

  [[nodiscard]] virtual Type GetType() const = 0;
  [[nodiscard]] virtual bool Movable() { return !pinned_; }

  [[nodiscard]] virtual float GetHeight() const { return 0.f; };
  [[nodiscard]] bool GetVisible() const { return visible_; }
  void SetVisible(bool value) { visible_ = value; }

  void SetColor(const Color& color) { color_ = color; }

  [[nodiscard]] uint32_t GetNumTimers() const { return num_timers_; }
  [[nodiscard]] virtual uint64_t GetMinTime() const { return min_time_; }
  [[nodiscard]] virtual uint64_t GetMaxTime() const { return max_time_; }
  void SetNumberOfPrioritizedTrailingCharacters(int num_characters) {
    num_prioritized_trailing_characters_ = num_characters;
  }
  [[nodiscard]] int GetNumberOfPrioritizedTrailingCharacters() const {
    return num_prioritized_trailing_characters_;
  }

  [[nodiscard]] virtual std::vector<std::shared_ptr<TimerChain>> GetTimers() const { return {}; }
  [[nodiscard]] virtual std::vector<std::shared_ptr<TimerChain>> GetAllChains() const { return {}; }
  [[nodiscard]] virtual std::vector<std::shared_ptr<TimerChain>> GetAllSerializableChains() const {
    return {};
  }

  [[nodiscard]] bool IsPinned() const { return pinned_; }
  void SetPinned(bool value);

  [[nodiscard]] bool IsMoving() const { return picked_ && mouse_pos_last_click_ != mouse_pos_cur_; }
  void SetName(const std::string& name) { name_ = name; }
  [[nodiscard]] const std::string& GetName() const { return name_; }
  void SetLabel(const std::string& label) { label_ = label; }
  [[nodiscard]] const std::string& GetLabel() const { return label_; }

  [[nodiscard]] Color GetBackgroundColor() const;

  void AddChild(const std::shared_ptr<Track>& track) { children_.emplace_back(track); }
  virtual void OnCollapseToggle(TriangleToggle::State state);
  [[nodiscard]] virtual bool IsCollapsible() const { return false; }
  [[nodiscard]] int32_t GetProcessId() const { return process_id_; }
  void SetProcessId(uint32_t pid) { process_id_ = pid; }
  [[nodiscard]] virtual bool IsEmpty() const = 0;

  [[nodiscard]] virtual bool IsTrackSelected() const { return false; }

  [[nodiscard]] bool IsCollapsed() const { return collapse_toggle_->IsCollapsed(); }

  // Accessibility
  [[nodiscard]] const orbit_gl::AccessibleTrack* AccessibilityInterface() const {
    return &accessibility_;
  }

 protected:
  void DrawTriangleFan(Batcher* batcher, const std::vector<Vec2>& points, const Vec2& pos,
                       const Color& color, float rotation, float z);

  std::string name_;
  std::string label_;
  int num_prioritized_trailing_characters_;
  int32_t thread_id_;
  int32_t process_id_;
  Color color_;
  bool visible_ = true;
  bool pinned_ = false;
  std::atomic<uint32_t> num_timers_;
  std::atomic<uint64_t> min_time_;
  std::atomic<uint64_t> max_time_;
  Type type_ = kUnknown;
  std::vector<std::shared_ptr<Track>> children_;
  std::shared_ptr<TriangleToggle> collapse_toggle_;

  orbit_gl::AccessibleTrack accessibility_;
  const CaptureData* capture_data_ = nullptr;
};

#endif