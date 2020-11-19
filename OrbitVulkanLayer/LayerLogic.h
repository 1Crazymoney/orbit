// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_LAYER_LOGIC_H_
#define ORBIT_VULKAN_LAYER_LAYER_LOGIC_H_

#include "CommandBufferManager.h"
#include "DeviceManager.h"
#include "DispatchTable.h"
#include "OrbitBase/Logging.h"
#include "OrbitService/ProducerSideUnixDomainSocketPath.h"
#include "QueueManager.h"
#include "TimerQueryPool.h"
#include "VulkanLayerProducer.h"
#include "vulkan/vulkan.h"

namespace orbit_vulkan_layer {

/**
 * This class controls the logic of this layer. For the instrumented vulkan functions,
 * it provides PreCall*, PostCall* and Call* functions, where the Call* functions just forward
 * to the next layer (using the dispatch table).
 * PreCall* functions are executed before the `actual` vulkan call and PostCall* afterwards.
 * PreCall/PostCall are omitted when not needed.
 *
 * Usage: For an instrumented vulkan function "X" a common pattern from the layers entry (Main.cpp)
 * would be:
 * ```
 * logic_.PreCallX(...);
 * logic_.CallX(...);
 * logic_.PostCallX(...);
 * ```
 */
class LayerLogic {
 public:
  LayerLogic()
      : vulkan_layer_producer_{std::nullopt},
        device_manager_(&dispatch_table_),
        timer_query_pool_(&dispatch_table_),
        command_buffer_manager_(&dispatch_table_, &timer_query_pool_, &device_manager_,
                                &vulkan_layer_producer_) {
    LOG("LayerLogic");
  }

  ~LayerLogic() { CloseVulkanLayerProducerIfNecessary(); }

  [[nodiscard]] VkResult PreCallAndCallCreateInstance(const VkInstanceCreateInfo* create_info,
                                                      const VkAllocationCallbacks* allocator,
                                                      VkInstance* instance);
  void PostCallCreateInstance(const VkInstanceCreateInfo* create_info,
                              const VkAllocationCallbacks* allocator, VkInstance* instance);

  [[nodiscard]] PFN_vkVoidFunction CallGetDeviceProcAddr(VkDevice device, const char* name) {
    LOG("CallGetDeviceProcAddr(%s)", name);
    return dispatch_table_.GetDeviceProcAddr(device)(device, name);
  }

  [[nodiscard]] PFN_vkVoidFunction CallGetInstanceProcAddr(VkInstance instance, const char* name) {
    LOG("CallGetInstanceProcAddr(%s)", name);
    return dispatch_table_.GetInstanceProcAddr(instance)(instance, name);
  }

  void CallAndPostDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    LOG("CallAndPostDestroyInstance");
    PFN_vkDestroyInstance destroy_instance_function = dispatch_table_.DestroyInstance(instance);
    CHECK(destroy_instance_function != nullptr);
    dispatch_table_.RemoveInstanceDispatchTable(instance);

    destroy_instance_function(instance, allocator);

    CloseVulkanLayerProducerIfNecessary();
  }

  void CallAndPostDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    LOG("CallAndPostDestroyDevice");
    PFN_vkDestroyDevice destroy_device_function = dispatch_table_.DestroyDevice(device);
    CHECK(destroy_device_function != nullptr);
    device_manager_.UntrackLogicalDevice(device);
    dispatch_table_.RemoveDeviceDispatchTable(device);

    destroy_device_function(device, allocator);
  }

  [[nodiscard]] VkResult PreCallAndCallCreateDevice(VkPhysicalDevice physical_device,
                                                    const VkDeviceCreateInfo* create_info,
                                                    const VkAllocationCallbacks* allocator,
                                                    VkDevice* device);
  void PostCallCreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                            const VkAllocationCallbacks* allocator, VkDevice* device);

  [[nodiscard]] VkResult CallEnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device,
                                                                const char* layer_name,
                                                                uint32_t* property_count,
                                                                VkExtensionProperties* properties) {
    LOG("CallEnumerateDeviceExtensionProperties");
    return dispatch_table_.EnumerateDeviceExtensionProperties(physical_device)(
        physical_device, layer_name, property_count, properties);
  }

  [[nodiscard]] VkResult CallResetCommandPool(VkDevice device, VkCommandPool command_pool,
                                              VkCommandPoolResetFlags flags) {
    LOG("CallResetCommandPool");
    return dispatch_table_.ResetCommandPool(device)(device, command_pool, flags);
  }
  void PostCallResetCommandPool(VkDevice device, VkCommandPool command_pool,
                                VkCommandPoolResetFlags flags);
  [[nodiscard]] VkResult CallAllocateCommandBuffers(
      VkDevice device, const VkCommandBufferAllocateInfo* allocate_info,
      VkCommandBuffer* command_buffers) {
    LOG("CallAllocateCommandBuffers");
    return dispatch_table_.AllocateCommandBuffers(device)(device, allocate_info, command_buffers);
  }
  void PostCallAllocateCommandBuffers(VkDevice device,
                                      const VkCommandBufferAllocateInfo* allocate_info,
                                      VkCommandBuffer* command_buffers);

  void CallFreeCommandBuffers(VkDevice device, VkCommandPool command_pool,
                              uint32_t command_buffer_count,
                              const VkCommandBuffer* command_buffers) {
    LOG("CallFreeCommandBuffers");
    return dispatch_table_.FreeCommandBuffers(device)(device, command_pool, command_buffer_count,
                                                      command_buffers);
  }
  void PostCallFreeCommandBuffers(VkDevice device, VkCommandPool command_pool,
                                  uint32_t command_buffer_count,
                                  const VkCommandBuffer* command_buffers);
  [[nodiscard]] VkResult CallBeginCommandBuffer(VkCommandBuffer command_buffer,
                                                const VkCommandBufferBeginInfo* begin_info) {
    LOG("CallBeginCommandBuffer");
    return dispatch_table_.BeginCommandBuffer(command_buffer)(command_buffer, begin_info);
  }
  void PostCallBeginCommandBuffer(VkCommandBuffer command_buffer,
                                  const VkCommandBufferBeginInfo* begin_info);

  void PreCallEndCommandBuffer(VkCommandBuffer command_buffer);
  [[nodiscard]] VkResult CallEndCommandBuffer(VkCommandBuffer command_buffer) {
    LOG("CallEndCommandBuffer");
    return dispatch_table_.EndCommandBuffer(command_buffer)(command_buffer);
  }

  void PreCallResetCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags);
  [[nodiscard]] VkResult CallResetCommandBuffer(VkCommandBuffer command_buffer,
                                                VkCommandBufferResetFlags flags) {
    LOG("CallResetCommandBuffer");
    return dispatch_table_.ResetCommandBuffer(command_buffer)(command_buffer, flags);
  }

  void CallGetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index,
                          VkQueue* queue) {
    LOG("CallGetDeviceQueue");
    return dispatch_table_.GetDeviceQueue(device)(device, queue_family_index, queue_index, queue);
  }
  void PostCallGetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index,
                              VkQueue* queue);

  void CallGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* queue_info, VkQueue* queue) {
    LOG("CallGetDeviceQueue2");
    return dispatch_table_.GetDeviceQueue2(device)(device, queue_info, queue);
  }
  void PostCallGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* queue_info,
                               VkQueue* queue);

  void PreCallQueueSubmit(VkQueue queue, uint32_t submit_count, const VkSubmitInfo* submits,
                          VkFence fence);
  [[nodiscard]] VkResult CallQueueSubmit(VkQueue queue, uint32_t submit_count,
                                         const VkSubmitInfo* submits, VkFence fence) {
    LOG("CallQueueSubmit");
    return dispatch_table_.QueueSubmit(queue)(queue, submit_count, submits, fence);
  }
  void PostCallQueueSubmit(VkQueue queue, uint32_t submit_count, const VkSubmitInfo* submits,
                           VkFence fence);

  [[nodiscard]] VkResult CallQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* present_info) {
    LOG("CallQueuePresentKHR");
    return dispatch_table_.QueuePresentKHR(queue)(queue, present_info);
  }
  void PostCallQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* present_info);

  void CallCmdBeginDebugUtilsLabelEXT(VkCommandBuffer command_buffer,
                                      const VkDebugUtilsLabelEXT* label_info) {
    LOG("CallCmdBeginDebugUtilsLabelEXT");
    if (dispatch_table_.IsDebugUtilsExtensionSupported(command_buffer)) {
      dispatch_table_.CmdBeginDebugUtilsLabelEXT(command_buffer)(command_buffer, label_info);
    }
  }
  void PostCallCmdBeginDebugUtilsLabelEXT(VkCommandBuffer command_buffer,
                                          const VkDebugUtilsLabelEXT* label_info);

  void PreCallCmdEndDebugUtilsLabelEXT(VkCommandBuffer command_buffer);
  void CallCmdEndDebugUtilsLabelEXT(VkCommandBuffer command_buffer) {
    LOG("CallCmdEndDebugUtilsLabelEXT");
    if (dispatch_table_.IsDebugUtilsExtensionSupported(command_buffer)) {
      dispatch_table_.CmdEndDebugUtilsLabelEXT(command_buffer)(command_buffer);
    }
  }

  void CallCmdDebugMarkerBeginEXT(VkCommandBuffer command_buffer,
                                  const VkDebugMarkerMarkerInfoEXT* marker_info) {
    LOG("CallCmdDebugMarkerBeginEXT");
    if (dispatch_table_.IsDebugMarkerExtensionSupported(command_buffer)) {
      dispatch_table_.CmdDebugMarkerBeginEXT(command_buffer)(command_buffer, marker_info);
    }
  }
  void PostCallCmdDebugMarkerBeginEXT(VkCommandBuffer command_buffer,
                                      const VkDebugMarkerMarkerInfoEXT* marker_info);

  void PreCallCmdDebugMarkerEndEXT(VkCommandBuffer command_buffer);
  void CallCmdDebugMarkerEndEXT(VkCommandBuffer command_buffer) {
    LOG("CallCmdDebugMarkerEndEXT");
    if (dispatch_table_.IsDebugMarkerExtensionSupported(command_buffer)) {
      dispatch_table_.CmdDebugMarkerEndEXT(command_buffer)(command_buffer);
    }
  }

 private:
  void InitVulkanLayerProducerIfNecessary() {
    absl::MutexLock lock{&vulkan_layer_producer_mutex_};
    if (!vulkan_layer_producer_.has_value()) {
      vulkan_layer_producer_.emplace();
      if (!vulkan_layer_producer_->BringUp(orbit_service::kProducerSideUnixDomainSocketPath)) {
        vulkan_layer_producer_.reset();
      }
    }
  }

  void CloseVulkanLayerProducerIfNecessary() {
    absl::MutexLock lock{&vulkan_layer_producer_mutex_};
    if (vulkan_layer_producer_.has_value()) {
      // TODO: Only do this when DestroyInstance has been called a number of times
      //  equal to the number of times CreateInstance was called.
      vulkan_layer_producer_->TakeDown();
      vulkan_layer_producer_.reset();
    }
  }

  std::optional<VulkanLayerProducer> vulkan_layer_producer_;
  absl::Mutex vulkan_layer_producer_mutex_;

  DispatchTable dispatch_table_;
  DeviceManager<DispatchTable> device_manager_;
  TimerQueryPool timer_query_pool_;
  CommandBufferManager command_buffer_manager_;
  QueueManager queue_manager_;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_LAYER_LOGIC_H_
