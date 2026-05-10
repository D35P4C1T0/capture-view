#pragma once

#include "cli.hpp"

#include <vector>

namespace cv {

void load_config(CliOptions& options);
void save_config(const CliOptions& options);
std::string config_file_path();
std::string config_directory_path();
std::vector<std::string> list_config_profiles();
void init_default_profiles();
void set_config_location(const CliOptions& options);

} // namespace cv
