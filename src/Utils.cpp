#include "Utils.h"

#include <err.h>
#include <fts.h>
#include <unistd.h>

#include <string>
#include <vector>

std::string getWorkingDir() {
  char buf[PATH_MAX];
  if (!getcwd(buf, sizeof(buf))) {
    err(1, "getcwd failed");
  }
  return buf;
}

std::vector<std::string> collectFiles(const char* directory) {
  std::vector<std::string> files;

  char* dir_argv[2] = { const_cast<char*>(directory), nullptr };
  FTS* fts = fts_open(dir_argv, FTS_LOGICAL | FTS_NOCHDIR, nullptr);

  if (!fts) {
    err(1, "failed to open directory '%s'", directory);
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
