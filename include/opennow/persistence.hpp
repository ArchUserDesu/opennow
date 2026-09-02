#pragma once
#include "models.hpp"
#include <string>
namespace opennow { bool save_session(const AuthSession&,const std::string& path); bool load_session(AuthSession&,const std::string& path); }
