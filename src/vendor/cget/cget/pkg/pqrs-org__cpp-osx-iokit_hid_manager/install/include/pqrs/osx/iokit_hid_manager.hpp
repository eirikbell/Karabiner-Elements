#pragma once

// pqrs::iokit_hid_manager v2.1

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

// `pqrs::osx::iokit_hid_manager` can be used safely in a multi-threaded environment.

#include <IOKit/hid/IOHIDDevice.h>
#include <pqrs/cf_number.hpp>
#include <pqrs/osx/iokit_service_monitor.hpp>
#include <unordered_map>
#include <vector>

namespace pqrs {
namespace osx {
class iokit_hid_manager final : public pqrs::dispatcher::extra::dispatcher_client {
public:
  // Signals (invoked from the shared dispatcher thread)

  nod::signal<void(iokit_registry_entry_id, cf_ptr<IOHIDDeviceRef>)> device_matched;
  nod::signal<void(iokit_registry_entry_id)> device_terminated;
  nod::signal<void(const std::string&, iokit_return)> error_occurred;

  // Methods

  iokit_hid_manager(const iokit_hid_manager&) = delete;

  iokit_hid_manager(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
                    const std::vector<cf_ptr<CFDictionaryRef>>& matching_dictionaries) : dispatcher_client(weak_dispatcher),
                                                                                         matching_dictionaries_(matching_dictionaries) {
  }

  virtual ~iokit_hid_manager(void) {
    detach_from_dispatcher([this] {
      stop();
    });
  }

  void async_start(void) {
    enqueue_to_dispatcher([this] {
      start();
    });
  }

  static cf_ptr<CFDictionaryRef> make_matching_dictionary(iokit_hid_usage_page hid_usage_page,
                                                          iokit_hid_usage hid_usage) {
    cf_ptr<CFDictionaryRef> result;

    if (auto matching_dictionary = IOServiceMatching(kIOHIDDeviceKey)) {
      if (auto number = pqrs::make_cf_number(type_safe::get(hid_usage_page))) {
        CFDictionarySetValue(matching_dictionary,
                             CFSTR(kIOHIDDeviceUsagePageKey),
                             *number);
      }
      if (auto number = pqrs::make_cf_number(type_safe::get(hid_usage))) {
        CFDictionarySetValue(matching_dictionary,
                             CFSTR(kIOHIDDeviceUsageKey),
                             *number);
      }

      result = matching_dictionary;

      CFRelease(matching_dictionary);
    }

    return result;
  }

  static cf_ptr<CFDictionaryRef> make_matching_dictionary(iokit_hid_usage_page hid_usage_page) {
    cf_ptr<CFDictionaryRef> result;

    if (auto matching_dictionary = IOServiceMatching(kIOHIDDeviceKey)) {
      if (auto number = pqrs::make_cf_number(type_safe::get(hid_usage_page))) {
        CFDictionarySetValue(matching_dictionary,
                             CFSTR(kIOHIDDeviceUsagePageKey),
                             *number);
      }

      result = matching_dictionary;

      CFRelease(matching_dictionary);
    }

    return result;
  }

private:
  // This method is executed in the dispatcher thread.
  void start(void) {
    for (const auto& matching_dictionary : matching_dictionaries_) {
      if (matching_dictionary) {
        auto monitor = std::make_shared<pqrs::osx::iokit_service_monitor>(weak_dispatcher_,
                                                                          *matching_dictionary);

        monitor->service_matched.connect([this](auto&& registry_entry_id, auto&& service_ptr) {
          if (devices_.find(registry_entry_id) == std::end(devices_)) {
            if (auto device = IOHIDDeviceCreate(kCFAllocatorDefault, *service_ptr)) {
              auto device_ptr = pqrs::cf_ptr<IOHIDDeviceRef>(device);
              devices_[registry_entry_id] = device_ptr;

              enqueue_to_dispatcher([this, registry_entry_id, device_ptr] {
                device_matched(registry_entry_id, device_ptr);
              });

              CFRelease(device);
            }
          }
        });

        monitor->service_terminated.connect([this](auto&& registry_entry_id) {
          auto it = devices_.find(registry_entry_id);
          if (it != std::end(devices_)) {
            devices_.erase(registry_entry_id);

            enqueue_to_dispatcher([this, registry_entry_id] {
              device_terminated(registry_entry_id);
            });
          }
        });

        monitor->error_occurred.connect([this](auto&& message, auto&& r) {
          enqueue_to_dispatcher([this, message, r] {
            error_occurred(message, r);
          });
        });

        monitor->async_start();

        service_monitors_.push_back(monitor);
      }
    }
  }

  // This method is executed in the dispatcher thread.
  void stop(void) {
    service_monitors_.clear();
    devices_.clear();
  }

  std::vector<cf_ptr<CFDictionaryRef>> matching_dictionaries_;

  std::vector<std::shared_ptr<iokit_service_monitor>> service_monitors_;
  std::unordered_map<pqrs::osx::iokit_registry_entry_id, pqrs::cf_ptr<IOHIDDeviceRef>> devices_;
};
} // namespace osx
} // namespace pqrs
