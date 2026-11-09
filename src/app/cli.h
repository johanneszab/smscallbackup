// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef CLI_H
#define CLI_H

// Command-line mode:
//   smscallbackup --backup [FOLDER] [--incremental | --new-files]
//   smscallbackup --restore FILE... [--sim N] [--keep-unread] [--dry-run]
//   smscallbackup --info FILE...
//   smscallbackup --scheduled [--force]   (the automatic backup, run by a systemd timer)
namespace Cli {

bool wanted(int argc, char **argv);
int run(int argc, char **argv);

} // namespace Cli

#endif // CLI_H
