#include "boost_defs.hpp"

#include "hid_manager.hpp"
#include "hid_observer.hpp"
#include <boost/optional/optional_io.hpp>

namespace {
class dump_hid_value final {
public:
  dump_hid_value(const dump_hid_value&) = delete;

  dump_hid_value(void) {
    std::vector<std::pair<krbn::hid_usage_page, krbn::hid_usage>> targets({
        std::make_pair(krbn::hid_usage_page::generic_desktop, krbn::hid_usage::gd_keyboard),
        std::make_pair(krbn::hid_usage_page::generic_desktop, krbn::hid_usage::gd_mouse),
        std::make_pair(krbn::hid_usage_page::generic_desktop, krbn::hid_usage::gd_pointer),
    });

    hid_manager_ = std::make_unique<krbn::hid_manager>(targets);

    hid_manager_->device_detected.connect([this](auto&& weak_hid) {
      if (auto hid = weak_hid.lock()) {
        hid->values_arrived.connect([this, weak_hid](auto&& weak_event_queue) {
          if (auto hid = weak_hid.lock()) {
            if (auto eq = weak_event_queue.lock()) {
              values_arrived(hid, eq);
            }
          }
        });

        // Observe

        auto hid_observer = std::make_shared<krbn::hid_observer>(hid);

        hid_observer->device_observed.connect([weak_hid] {
          if (auto hid = weak_hid.lock()) {
            krbn::logger::get_logger().info("{0} is observed.", hid->get_name_for_log());
          }
        });

        hid_observer->device_unobserved.connect([weak_hid] {
          if (auto hid = weak_hid.lock()) {
            krbn::logger::get_logger().info("{0} is unobserved.", hid->get_name_for_log());
          }
        });

        hid_observer->async_observe();

        hid_observers_[hid->get_registry_entry_id()] = hid_observer;
      }
    });

    hid_manager_->device_removed.connect([this](auto&& weak_hid) {
      if (auto hid = weak_hid.lock()) {
        krbn::logger::get_logger().info("{0} is removed.", hid->get_name_for_log());
        hid_observers_.erase(hid->get_registry_entry_id());
      }
    });

    hid_manager_->async_start();
  }

  ~dump_hid_value(void) {
    hid_manager_ = nullptr;

    hid_observers_.clear();
  }

private:
  void values_arrived(std::shared_ptr<krbn::human_interface_device> hid,
                      std::shared_ptr<krbn::event_queue> event_queue) {
    for (const auto& queued_event : event_queue->get_events()) {
      std::cout << queued_event.get_event_time_stamp().get_time_stamp() << " ";

      switch (queued_event.get_event().get_type()) {
        case krbn::event_queue::queued_event::event::type::none:
          std::cout << "none" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::key_code:
          if (auto key_code = queued_event.get_event().get_key_code()) {
            std::cout << "Key: " << std::dec << static_cast<uint32_t>(*key_code) << " "
                      << queued_event.get_event_type()
                      << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::consumer_key_code:
          if (auto consumer_key_code = queued_event.get_event().get_consumer_key_code()) {
            std::cout << "ConsumerKey: " << std::dec << static_cast<uint32_t>(*consumer_key_code) << " "
                      << queued_event.get_event_type()
                      << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::pointing_button:
          if (auto pointing_button = queued_event.get_event().get_pointing_button()) {
            std::cout << "Button: " << std::dec << static_cast<uint32_t>(*pointing_button) << " "
                      << queued_event.get_event_type()
                      << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::pointing_motion:
          if (auto pointing_motion = queued_event.get_event().get_pointing_motion()) {
            std::cout << "pointing_motion: " << pointing_motion->to_json() << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::shell_command:
          std::cout << "shell_command" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::select_input_source:
          std::cout << "select_input_source" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::set_variable:
          std::cout << "set_variable" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::mouse_key:
          std::cout << "mouse_key" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::stop_keyboard_repeat:
          std::cout << "stop_keyboard_repeat" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::device_keys_and_pointing_buttons_are_released:
          std::cout << "device_keys_and_pointing_buttons_are_released for " << hid->get_name_for_log() << " (" << hid->get_device_id() << ")" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::device_ungrabbed:
          std::cout << "device_ungrabbed for " << hid->get_name_for_log() << " (" << hid->get_device_id() << ")" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::caps_lock_state_changed:
          if (auto integer_value = queued_event.get_event().get_integer_value()) {
            std::cout << "caps_lock_state_changed " << *integer_value << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::pointing_device_event_from_event_tap:
          std::cout << "pointing_device_event_from_event_tap from " << hid->get_name_for_log() << " (" << hid->get_device_id() << ")" << std::endl;
          break;

        case krbn::event_queue::queued_event::event::type::frontmost_application_changed:
          if (auto frontmost_application = queued_event.get_event().get_frontmost_application()) {
            std::cout << "frontmost_application_changed "
                      << frontmost_application->get_bundle_identifier() << " "
                      << frontmost_application->get_file_path() << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::input_source_changed:
          if (auto input_source_identifiers = queued_event.get_event().get_input_source_identifiers()) {
            std::cout << "input_source_changed " << input_source_identifiers << std::endl;
          }
          break;

        case krbn::event_queue::queued_event::event::type::keyboard_type_changed:
          if (auto keyboard_type = queued_event.get_event().get_keyboard_type()) {
            std::cout << "keyboard_type_changed " << keyboard_type << std::endl;
          }
          break;

        default:
          std::cout << std::endl;
      }
    }
  }

  std::unique_ptr<krbn::hid_manager> hid_manager_;
  std::unordered_map<krbn::registry_entry_id, std::shared_ptr<krbn::hid_observer>> hid_observers_;
};
} // namespace

int main(int argc, const char* argv[]) {
  krbn::thread_utility::register_main_thread();

  dump_hid_value d;
  CFRunLoopRun();
  return 0;
}
