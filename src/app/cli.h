// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef CLI_H
#define CLI_H

// Command-line mode, e.g. for scheduled backups:
//   smscallbackup --backup [FOLDER]
//   smscallbackup --restore FILE... [--sim N] [--keep-unread] [--dry-run]
//   smscallbackup --info FILE...
namespace Cli {

bool wanted(int argc, char **argv);
int run(int argc, char **argv);

} // namespace Cli

#endif // CLI_H
