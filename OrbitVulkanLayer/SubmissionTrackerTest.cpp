// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "SubmissionTracker.h"
#include "VulkanLayerProducer.h"
#include "absl/base/casts.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::UnorderedElementsAre;

namespace orbit_vulkan_layer {
namespace {
class MockDispatchTable {
 public:
  MOCK_METHOD(PFN_vkGetQueryPoolResults, GetQueryPoolResults, (VkDevice), ());
  MOCK_METHOD(PFN_vkCmdWriteTimestamp, CmdWriteTimestamp, (VkCommandBuffer), ());
};

PFN_vkCmdWriteTimestamp dummy_write_timestamp_function =
    +[](VkCommandBuffer /*command_buffer*/, VkPipelineStageFlagBits /*pipeline_stage*/,
        VkQueryPool /*query_pool*/, uint32_t /*query*/) {};

class MockTimerQueryPool {
 public:
  MOCK_METHOD(VkQueryPool, GetQueryPool, (VkDevice), ());
  MOCK_METHOD(void, ResetQuerySlots, (VkDevice, const std::vector<uint32_t>&), ());
  MOCK_METHOD(void, RollbackPendingQuerySlots, (VkDevice, const std::vector<uint32_t>&), ());
  MOCK_METHOD(bool, NextReadyQuerySlot, (VkDevice, uint32_t*), ());
};

class MockDeviceManager {
 public:
  MOCK_METHOD(VkPhysicalDevice, GetPhysicalDeviceOfLogicalDevice, (VkDevice), ());
  MOCK_METHOD(VkPhysicalDeviceProperties, GetPhysicalDeviceProperties, (VkPhysicalDevice), ());
};

class MockVulkanLayerProducer : public VulkanLayerProducer {
 public:
  MOCK_METHOD(bool, IsCapturing, (), (override));
  MOCK_METHOD(uint64_t, InternStringIfNecessaryAndGetKey, (std::string), (override));
  MOCK_METHOD(void, EnqueueCaptureEvent, (orbit_grpc_protos::CaptureEvent && capture_event),
              (override));

  MOCK_METHOD(void, BringUp, (const std::shared_ptr<grpc::Channel>& channel), (override));
  MOCK_METHOD(void, TakeDown, (), (override));
};

}  // namespace

class SubmissionTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    producer = std::make_unique<MockVulkanLayerProducer>();
    auto is_capturing_function = [this]() -> bool { return is_capturing; };
    EXPECT_CALL(*producer, IsCapturing).WillRepeatedly(Invoke(is_capturing_function));
    EXPECT_CALL(timer_query_pool, GetQueryPool).WillRepeatedly(Return(query_pool));
    EXPECT_CALL(device_manager, GetPhysicalDeviceOfLogicalDevice)
        .WillRepeatedly(Return(physical_device));
    EXPECT_CALL(device_manager, GetPhysicalDeviceProperties)
        .WillRepeatedly(Return(physical_device_properties));

    EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
        .WillRepeatedly(Return(dummy_write_timestamp_function));
  }

  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer;
  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker =
      SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool>(
          0, &dispatch_table, &timer_query_pool, &device_manager,
          absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};
  VkPhysicalDevice physical_device = {};
  VkPhysicalDeviceProperties physical_device_properties = {.limits = {.timestampPeriod = 1.f}};
  VkQueue queue = {};
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .pCommandBuffers = &command_buffer,
                              .commandBufferCount = 1};

  bool is_capturing = false;

  static constexpr uint32_t kSlotIndex1 = 32;
  static constexpr uint32_t kSlotIndex2 = 33;
  static constexpr uint32_t kSlotIndex3 = 34;
  static constexpr uint32_t kSlotIndex4 = 35;

  const std::function<bool(VkDevice, uint32_t*)> mock_next_ready_query_slot_function_1 =
      [](VkDevice /*device*/, uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex1;
    return true;
  };

  const std::function<bool(VkDevice, uint32_t*)> mock_next_ready_query_slot_function_2 =
      [](VkDevice /*device*/, uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex2;
    return true;
  };

  const std::function<bool(VkDevice, uint32_t*)> mock_next_ready_query_slot_function_3 =
      [](VkDevice /*device*/, uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex3;
    return true;
  };

  const std::function<bool(VkDevice, uint32_t*)> mock_next_ready_query_slot_function_4 =
      [](VkDevice /*device*/, uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex4;
    return true;
  };

  static constexpr uint64_t kTimestamp1 = 11;
  static constexpr uint64_t kTimestamp2 = 12;
  static constexpr uint64_t kTimestamp3 = 13;
  static constexpr uint64_t kTimestamp4 = 14;

  const PFN_vkGetQueryPoolResults mock_get_query_pool_results_function_all_ready =
      +[](VkDevice /*device*/, VkQueryPool /*queryPool*/, uint32_t first_query,
          uint32_t query_count, size_t /*dataSize*/, void* data, VkDeviceSize /*stride*/,
          VkQueryResultFlags flags) -> VkResult {
    EXPECT_EQ(query_count, 1);
    EXPECT_NE((flags & VK_QUERY_RESULT_64_BIT), 0);
    switch (first_query) {
      case kSlotIndex1:
        *absl::bit_cast<uint64_t*>(data) = kTimestamp1;
        break;
      case kSlotIndex2:
        *absl::bit_cast<uint64_t*>(data) = kTimestamp2;
        break;
      case kSlotIndex3:
        *absl::bit_cast<uint64_t*>(data) = kTimestamp3;
        break;
      case kSlotIndex4:
        *absl::bit_cast<uint64_t*>(data) = kTimestamp4;
        break;
      default:
        UNREACHABLE();
    }
    return VK_SUCCESS;
  };

  const PFN_vkGetQueryPoolResults mock_get_query_pool_results_function_not_ready =
      +[](VkDevice /*device*/, VkQueryPool /*queryPool*/, uint32_t /*first_query*/,
          uint32_t /*query_count*/, size_t /*dataSize*/, void* /*data*/, VkDeviceSize /*stride*/,
          VkQueryResultFlags /*flags*/) -> VkResult { return VK_NOT_READY; };

  void EXPECT_SINGLE_COMMAND_BUFFER_SUBMISSION_EQ(
      const orbit_grpc_protos::CaptureEvent& actual_capture_event, uint64_t test_pre_submit_time,
      uint64_t test_post_submit_time, pid_t test_pid,
      uint64_t expected_command_buffer_begin_timestamp,
      uint64_t expected_command_buffer_end_timestamp) {
    EXPECT_TRUE(actual_capture_event.has_gpu_queue_submission());
    const orbit_grpc_protos::GpuQueueSubmission& actual_queue_submission =
        actual_capture_event.gpu_queue_submission();

    EXPECT_LE(test_pre_submit_time,
              actual_queue_submission.meta_info().pre_submission_cpu_timestamp());
    EXPECT_LE(actual_queue_submission.meta_info().pre_submission_cpu_timestamp(),
              actual_queue_submission.meta_info().post_submission_cpu_timestamp());
    EXPECT_LE(actual_queue_submission.meta_info().post_submission_cpu_timestamp(),
              test_post_submit_time);
    EXPECT_EQ(test_pid, actual_queue_submission.meta_info().tid());

    ASSERT_EQ(actual_queue_submission.submit_infos_size(), 1);
    const orbit_grpc_protos::GpuSubmitInfo& actual_submit_info =
        actual_queue_submission.submit_infos(0);

    ASSERT_EQ(actual_submit_info.command_buffers_size(), 1);
    const orbit_grpc_protos::GpuCommandBuffer& actual_command_buffer =
        actual_submit_info.command_buffers(0);

    EXPECT_EQ(expected_command_buffer_begin_timestamp,
              actual_command_buffer.begin_gpu_timestamp_ns());
    EXPECT_EQ(expected_command_buffer_end_timestamp, actual_command_buffer.end_gpu_timestamp_ns());
  }
};

TEST(SubmissionTracker, CanBeInitialized) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<VulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager, &producer);
}

TEST_F(SubmissionTrackerTest, CannotTrackTheSameCommandBufferTwice) {
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  EXPECT_DEATH({ tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1); }, "");
}

TEST_F(SubmissionTrackerTest, CannotUntrackAnUntrackedCommandBuffer) {
  EXPECT_DEATH({ tracker.UntrackCommandBuffers(device, command_pool, &command_buffer, 1); }, "");
}

TEST_F(SubmissionTrackerTest, CanTrackCommandBufferAgainAfterUntrack) {
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.UntrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
}

TEST_F(SubmissionTrackerTest, MarkCommandBufferBeginWontWriteTimestampsWhenNotCapturing) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);

  is_capturing = false;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
}

TEST_F(SubmissionTrackerTest, MarkDebugMarkersWontWriteTimestampsWhenNotCapturing) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);

  is_capturing = false;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkDebugMarkerBegin(command_buffer, "Test", {});
  tracker.MarkDebugMarkerEnd(command_buffer);
}

TEST_F(SubmissionTrackerTest, MarkCommandBufferBeginWillWriteTimestampWhenCapturing) {
  // We cannot capture anything in that lambda as we need to cast it to a function pointer.
  static bool was_called = false;

  PFN_vkCmdWriteTimestamp mock_write_timestamp_function =
      +[](VkCommandBuffer /*command_buffer*/, VkPipelineStageFlagBits /*pipeline_stage*/,
          VkQueryPool /*query_pool*/, uint32_t query) {
        EXPECT_EQ(query, kSlotIndex1);
        was_called = true;
      };

  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(1)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .Times(1)
      .WillOnce(Return(mock_write_timestamp_function));

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);

  EXPECT_TRUE(was_called);
  was_called = false;
}

TEST_F(SubmissionTrackerTest, ResetCommandBufferShouldRollbackUnsubmittedSlots) {
  EXPECT_CALL(*producer, IsCapturing).Times(1).WillOnce(Return(true));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(1)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1));
  std::vector<uint32_t> actual_slots_to_rollback;
  EXPECT_CALL(timer_query_pool, RollbackPendingQuerySlots)
      .Times(1)
      .WillOnce(SaveArg<1>(&actual_slots_to_rollback));

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.ResetCommandBuffer(command_buffer);

  EXPECT_THAT(actual_slots_to_rollback, ElementsAre(kSlotIndex1));
}

TEST_F(SubmissionTrackerTest, ResetCommandPoolShouldRollbackUnsubmittedSlots) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(1)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1));
  std::vector<uint32_t> actual_slots_to_rollback;
  EXPECT_CALL(timer_query_pool, RollbackPendingQuerySlots)
      .Times(1)
      .WillOnce(SaveArg<1>(&actual_slots_to_rollback));

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.ResetCommandPool(command_pool);

  EXPECT_THAT(actual_slots_to_rollback, ElementsAre(kSlotIndex1));
}

TEST_F(SubmissionTrackerTest, MarkCommandBufferEndWontWriteTimestampsWhenNotCapturing) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp).Times(0);

  is_capturing = false;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
}

TEST_F(SubmissionTrackerTest, MarkCommandBufferEndWontWriteTimestampsWhenNotCapturedBegin) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);
  is_capturing = false;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  is_capturing = true;
  tracker.MarkCommandBufferEnd(command_buffer);
}

TEST_F(SubmissionTrackerTest, CanRetrieveCommandBufferTimestampsForACompleteSubmission) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults)
      .WillRepeatedly(Return(mock_get_query_pool_results_function_all_ready));
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));
  orbit_grpc_protos::CaptureEvent actual_capture_event;
  auto mock_enqueue_capture_event =
      [&actual_capture_event](orbit_grpc_protos::CaptureEvent&& capture_event) {
        actual_capture_event = std::move(capture_event);
      };
  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(1).WillOnce(Invoke(mock_enqueue_capture_event));

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  pid_t pid = GetCurrentThreadId();
  uint64_t pre_submit_time = MonotonicTimestampNs();
  std::optional<uint64_t> pre_submit_timestamp = tracker.PreSubmission();
  tracker.DoPostSubmitQueue(queue, 1, &submit_info, pre_submit_timestamp);
  uint64_t post_submit_time = MonotonicTimestampNs();
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kSlotIndex1, kSlotIndex2));
  EXPECT_SINGLE_COMMAND_BUFFER_SUBMISSION_EQ(actual_capture_event, pre_submit_time,
                                             post_submit_time, pid, kTimestamp1, kTimestamp2);
}

TEST_F(SubmissionTrackerTest,
       CanRetrieveCommandBufferTimestampsForACompleteSubmissionAtSecondPresent) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults)
      .WillOnce(Return(mock_get_query_pool_results_function_not_ready))
      .WillRepeatedly(Return(mock_get_query_pool_results_function_all_ready));
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));
  orbit_grpc_protos::CaptureEvent actual_capture_event;
  auto mock_enqueue_capture_event =
      [&actual_capture_event](orbit_grpc_protos::CaptureEvent&& capture_event) {
        actual_capture_event = std::move(capture_event);
      };
  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(1).WillOnce(Invoke(mock_enqueue_capture_event));

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  pid_t pid = GetCurrentThreadId();
  uint64_t pre_submit_time = MonotonicTimestampNs();
  std::optional<uint64_t> pre_submit_timestamp = tracker.PreSubmission();
  tracker.DoPostSubmitQueue(queue, 1, &submit_info, pre_submit_timestamp);
  uint64_t post_submit_time = MonotonicTimestampNs();
  tracker.CompleteSubmits(device);
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kSlotIndex1, kSlotIndex2));
  EXPECT_SINGLE_COMMAND_BUFFER_SUBMISSION_EQ(actual_capture_event, pre_submit_time,
                                             post_submit_time, pid, kTimestamp1, kTimestamp2);
}

TEST_F(SubmissionTrackerTest, StopCaptureBeforeSubmissionWillResetTheSlots) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults).Times(0);
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));

  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(0);

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  is_capturing = false;
  std::optional<uint64_t> pre_submit_timestamp = tracker.PreSubmission();
  tracker.DoPostSubmitQueue(queue, 1, &submit_info, pre_submit_timestamp);
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kSlotIndex1, kSlotIndex2));
}

TEST_F(SubmissionTrackerTest, CanRetrieveCommandBufferTimestampsWhenNotCapturingAtPresent) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults)
      .WillRepeatedly(Return(mock_get_query_pool_results_function_all_ready));
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));
  orbit_grpc_protos::CaptureEvent actual_capture_event;
  auto mock_enqueue_capture_event =
      [&actual_capture_event](orbit_grpc_protos::CaptureEvent&& capture_event) {
        actual_capture_event = std::move(capture_event);
      };
  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(1).WillOnce(Invoke(mock_enqueue_capture_event));

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  pid_t pid = GetCurrentThreadId();
  uint64_t pre_submit_time = MonotonicTimestampNs();
  std::optional<uint64_t> pre_submit_timestamp = tracker.PreSubmission();
  tracker.DoPostSubmitQueue(queue, 1, &submit_info, pre_submit_timestamp);
  uint64_t post_submit_time = MonotonicTimestampNs();
  is_capturing = false;
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kSlotIndex1, kSlotIndex2));
  EXPECT_SINGLE_COMMAND_BUFFER_SUBMISSION_EQ(actual_capture_event, pre_submit_time,
                                             post_submit_time, pid, kTimestamp1, kTimestamp2);
}

TEST_F(SubmissionTrackerTest, StopCaptureWhileSubmissionWillResetTheSlots) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults).Times(0);
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));

  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(0);

  is_capturing = true;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  std::optional<uint64_t> pre_submit_timestamp = tracker.PreSubmission();
  is_capturing = false;
  tracker.DoPostSubmitQueue(queue, 1, &submit_info, pre_submit_timestamp);
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kSlotIndex1, kSlotIndex2));
}

TEST_F(SubmissionTrackerTest, StartCaptureWhileSubmissionWillNotCrash) {
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);
  EXPECT_CALL(dispatch_table, GetQueryPoolResults).Times(0);
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(0);
  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(0);

  is_capturing = false;
  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  std::optional<uint64_t> pre_submit_timestamp = tracker.PreSubmission();
  is_capturing = true;
  tracker.DoPostSubmitQueue(queue, 1, &submit_info, pre_submit_timestamp);
  tracker.CompleteSubmits(device);
}

// TODO: debug marker begin
// TODO: debug marker end
// TODO: nested debug markers
// TODO: debug markers across submissions
// TODO: debug markers across submissions, miss begin marker (not capturing)
// TODO: debug markers across submissions, miss end marker (not capturing)
// TODO: debug markers, stop capturing before submission
// TODO: debug markers limited by depth
// TODO: Reuse command buffer without reset will fail

}  // namespace orbit_vulkan_layer