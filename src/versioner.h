/*
 * Copyright (C) 2016 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <unordered_map>

extern bool verbose;

static const std::set<std::string> supported_archs = {
  "arm", "arm64", "mips", "mips64", "x86", "x86_64",
};

static std::unordered_map<std::string, std::string> arch_targets = {
  { "arm", "arm-linux-androideabi" },
  { "arm64", "aarch64-linux-android" },
  { "mips", "mipsel-linux-android" },
  { "mips64", "mips64el-linux-android" },
  { "x86", "i686-linux-android" },
  { "x86_64", "x86_64-linux-android" },
};

static const std::set<int> supported_levels = { 9, 12, 13, 14, 15, 16, 17, 18, 19, 21, 23, 24 };

// Non-const for the convenience of being able to index with operator[].
static std::map<std::string, int> arch_min_api = {
  { "arm", 9 },
  { "arm64", 21 },
  { "mips", 9 },
  { "mips64", 21 },
  { "x86", 9 },
  { "x86_64", 21 },
};

static const std::unordered_map<std::string, std::set<std::string>> header_blacklist = {
  // Internal header.
  { "sys/_system_properties.h", supported_archs },

  // time64.h #errors when included on LP64 archs.
  { "time64.h", { "arm64", "mips64", "x86_64" } },
};


