#include "hid_observer.hpp"
#include "libkrbn.h"
#include "libkrbn_cpp.hpp"
#include <pqrs/osx/iokit_hid_manager.hpp>
#include <unordered_set>

namespace {
class libkrbn_hid_value_observer_class final {
public:
  libkrbn_hid_value_observer_class(const libkrbn_hid_value_observer_class&) = delete;

  libkrbn_hid_value_observer_class(libkrbn_hid_value_observer_callback callback,
                                   void* refcon) : callback_(callback),
                                                   refcon_(refcon) {
    std::vector<pqrs::cf_ptr<CFDictionaryRef>> matching_dictionaries{
        pqrs::osx::iokit_hid_manager::make_matching_dictionary(
            pqrs::osx::iokit_hid_usage_page_generic_desktop,
            pqrs::osx::iokit_hid_usage_generic_desktop_keyboard),
    };

    hid_manager_ = std::make_unique<pqrs::osx::iokit_hid_manager>(pqrs::dispatcher::extra::get_shared_dispatcher(),
                                                                  matching_dictionaries);

    hid_manager_->device_matched.connect([this](auto&& registry_entry_id, auto&& device_ptr) {
      auto hid = std::make_shared<krbn::human_interface_device>(*device_ptr,
                                                                registry_entry_id);
      hids_[registry_entry_id] = hid;

      hid->values_arrived.connect([this](auto&& event_queue) {
        values_arrived(event_queue);
      });

      auto hid_observer = std::make_shared<krbn::hid_observer>(hid);
      hid_observers_[hid->get_registry_entry_id()] = hid_observer;

      hid_observer->device_observed.connect([this, registry_entry_id] {
        std::lock_guard<std::mutex> lock(observed_devices_mutex_);

        observed_devices_.insert(registry_entry_id);
      });

      hid_observer->device_unobserved.connect([this, registry_entry_id] {
        std::lock_guard<std::mutex> lock(observed_devices_mutex_);

        observed_devices_.erase(registry_entry_id);
      });

      hid_observer->async_observe();
    });

    hid_manager_->device_terminated.connect([this](auto&& registry_entry_id) {
      hid_observers_.erase(registry_entry_id);
      hids_.erase(registry_entry_id);

      {
        std::lock_guard<std::mutex> lock(observed_devices_mutex_);

        observed_devices_.erase(registry_entry_id);
      }
    });

    hid_manager_->error_occurred.connect([](auto&& message, auto&& iokit_return) {
      krbn::logger::get_logger().error("{0}: {1}", message, iokit_return.to_string());
    });

    hid_manager_->async_start();
  }

  ~libkrbn_hid_value_observer_class(void) {
    hid_manager_ = nullptr;

    hid_observers_.clear();
    hids_.clear();
  }

  size_t calculate_observed_device_count(void) const {
    std::lock_guard<std::mutex> lock(observed_devices_mutex_);

    return observed_devices_.size();
  }

private:
  void values_arrived(std::shared_ptr<krbn::event_queue::queue> event_queue) {
    for (const auto& entry : event_queue->get_entries()) {
      libkrbn_hid_value_event_type event_type = libkrbn_hid_value_event_type_key_down;
      switch (entry.get_event_type()) {
        case krbn::event_type::key_down:
          event_type = libkrbn_hid_value_event_type_key_down;
          break;
        case krbn::event_type::key_up:
          event_type = libkrbn_hid_value_event_type_key_up;
          break;
        case krbn::event_type::single:
          event_type = libkrbn_hid_value_event_type_single;
          break;
      }

      switch (entry.get_event().get_type()) {
        case krbn::event_queue::event::type::key_code:
          if (auto key_code = entry.get_event().get_key_code()) {
            callback_(libkrbn_hid_value_type_key_code,
                      static_cast<uint32_t>(*key_code),
                      event_type,
                      refcon_);
          }
          break;

        case krbn::event_queue::event::type::consumer_key_code:
          if (auto consumer_key_code = entry.get_event().get_consumer_key_code()) {
            callback_(libkrbn_hid_value_type_consumer_key_code,
                      static_cast<uint32_t>(*consumer_key_code),
                      event_type,
                      refcon_);
          }
          break;

        case krbn::event_queue::event::type::none:
        case krbn::event_queue::event::type::pointing_button:
        case krbn::event_queue::event::type::pointing_motion:
        case krbn::event_queue::event::type::shell_command:
        case krbn::event_queue::event::type::select_input_source:
        case krbn::event_queue::event::type::set_variable:
        case krbn::event_queue::event::type::mouse_key:
        case krbn::event_queue::event::type::stop_keyboard_repeat:
        case krbn::event_queue::event::type::device_keys_and_pointing_buttons_are_released:
        case krbn::event_queue::event::type::device_ungrabbed:
        case krbn::event_queue::event::type::caps_lock_state_changed:
        case krbn::event_queue::event::type::pointing_device_event_from_event_tap:
        case krbn::event_queue::event::type::frontmost_application_changed:
        case krbn::event_queue::event::type::input_source_changed:
        case krbn::event_queue::event::type::keyboard_type_changed:
          break;
      }
    }
  }

  libkrbn_hid_value_observer_callback callback_;
  void* refcon_;

  std::unique_ptr<pqrs::osx::iokit_hid_manager> hid_manager_;
  std::unordered_map<pqrs::osx::iokit_registry_entry_id, std::shared_ptr<krbn::human_interface_device>> hids_;
  std::unordered_map<pqrs::osx::iokit_registry_entry_id, std::shared_ptr<krbn::hid_observer>> hid_observers_;

  std::unordered_set<pqrs::osx::iokit_registry_entry_id> observed_devices_;
  mutable std::mutex observed_devices_mutex_;
};
} // namespace

bool libkrbn_hid_value_observer_initialize(libkrbn_hid_value_observer** out, libkrbn_hid_value_observer_callback callback, void* refcon) {
  if (!out) return false;
  // return if already initialized.
  if (*out) return false;

  *out = reinterpret_cast<libkrbn_hid_value_observer*>(new libkrbn_hid_value_observer_class(callback, refcon));
  return true;
}

void libkrbn_hid_value_observer_terminate(libkrbn_hid_value_observer** p) {
  if (p && *p) {
    delete reinterpret_cast<libkrbn_hid_value_observer_class*>(*p);
    *p = nullptr;
  }
}

size_t libkrbn_hid_value_observer_calculate_observed_device_count(libkrbn_hid_value_observer* p) {
  if (p) {
    if (auto o = reinterpret_cast<libkrbn_hid_value_observer_class*>(p)) {
      return o->calculate_observed_device_count();
    }
  }
  return 0;
}
