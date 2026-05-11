#include "app.hpp"
#include "cli.hpp"
#include "common.hpp"
#include "config.hpp"
#include "log.hpp"

#include <iostream>

int main(int argc, char** argv) {
  try {
    const cv::CliOptions cli_only = cv::parse_cli(argc, argv);
    cv::CliOptions merged;
    merged.no_config = cli_only.no_config;
    merged.profile = cli_only.profile;
    merged.config_file = cli_only.config_file;
    cv::set_config_location(merged);
    cv::load_config(merged);
    merged = cv::parse_cli(argc, argv, merged);
    cv::log::init(merged.log_file, merged.verbose);
    cv::log::info("capture-view start version=0.1.0");
    const int result = cv::run_app(merged);
    if (merged.save_config ||
        (!merged.diagnostic_bundle && !merged.list_profiles && !merged.init_profiles && !merged.doctor && !merged.list_devices &&
         !merged.list_audio && !merged.gtk_ui && !merged.test_pattern &&
         !merged.benchmark_seconds)) {
      cv::save_config(merged);
      cv::log::info("config saved: ", cv::config_file_path());
    }
    return result;
  } catch (const cv::AppError& error) {
    cv::log::error(error.what());
    std::cerr << "error: " << error.what() << "\n\n";
    cv::print_usage(argv[0]);
    return 2;
  } catch (const std::exception& error) {
    cv::log::error("fatal: ", error.what());
    std::cerr << "fatal: " << error.what() << "\n";
    return 1;
  }
}
