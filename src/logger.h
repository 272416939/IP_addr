#pragma once

#include "Entry/Entry.h"
#include "ll/api/io/Logger.h"

namespace IPA {
inline ll::io::Logger& logger = Entry::getInstance().getSelf().getLogger(); // logger.Trace Debug Info Warn Error Fatal
} // namespace IPA
