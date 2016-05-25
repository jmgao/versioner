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

#include "Utils.h"

#include <err.h>
#include <fts.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.compare(0, prefix.length(), prefix) == 0;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  size_t suffix_length = suffix.length();
  size_t string_length = s.size();
  if (suffix_length > string_length) {
    return false;
  }
  size_t offset = string_length - suffix_length;
  return s.compare(offset, suffix_length, suffix) == 0;
}

std::string Trim(const std::string& s) {
  std::string result;

  if (s.size() == 0) {
    return result;
  }

  size_t start_index = 0;
  size_t end_index = s.size() - 1;

  // Skip initial whitespace.
  while (start_index < s.size()) {
    if (!isspace(s[start_index])) {
      break;
    }
    start_index++;
  }

  // Skip terminating whitespace.
  while (end_index >= start_index) {
    if (!isspace(s[end_index])) {
      break;
    }
    end_index--;
  }

  // All spaces, no beef.
  if (end_index < start_index) {
    return "";
  }
  // Start_index is the first non-space, end_index is the last one.
  return s.substr(start_index, end_index - start_index + 1);
}

std::string getWorkingDir() {
  char buf[PATH_MAX];
  if (!getcwd(buf, sizeof(buf))) {
    err(1, "getcwd failed");
  }
  return buf;
}

std::vector<std::string> collectFiles(const std::string& directory) {
  std::vector<std::string> files;

  char* dir_argv[2] = { const_cast<char*>(directory.c_str()), nullptr };
  FTS* fts = fts_open(dir_argv, FTS_LOGICAL | FTS_NOCHDIR, nullptr);

  if (!fts) {
    err(1, "failed to open directory '%s'", directory.c_str());
  }

  FTSENT* ent;
  while ((ent = fts_read(fts))) {
    if (ent->fts_info & (FTS_D | FTS_DP)) {
      continue;
    }

    files.push_back(ent->fts_path);
  }

  fts_close(fts);
  return files;
}
