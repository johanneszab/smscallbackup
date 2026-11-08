# SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
# SPDX-License-Identifier: MIT
Name:       smscallbackup
Summary:    Back up and restore SMS and call history in the SMS Backup & Restore format
Version:    0.1.0
Release:    1
License:    MIT
URL:        https://github.com/johanneszab/smscallbackup
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   qt5-plugin-sqldriver-sqlite
Requires:   sailfish-components-pickers-qt5

BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Concurrent)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(commhistory-qt5)
BuildRequires:  pkgconfig(zlib)
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils

%description
Backs up the SMS messages and the call log of Sailfish OS into XML files in the
format of the Android app "SMS Backup & Restore" (SyncTech), and restores such
files. Backups can be moved between Android and Sailfish OS in both directions.

Restoring adds only what is not on the phone yet. Restored messages go into the
existing conversations of the Messages app, calls into the call log of the
Phone app, without a restart. Compressed backups (.xml.zip) can be restored
too. MMS are not supported yet.

The app can also be used from the command line, e.g. for scheduled backups:
smscallbackup --backup [FOLDER], smscallbackup --restore FILE...

Not affiliated with SyncTech.

# This description section includes metadata for SailfishOS:Chum, see
# https://github.com/sailfishos-chum/main/blob/main/Metadata.md
%if 0%{?_chum}
Title: SMS & Call Backup
Type: desktop-application
DeveloperName: Johannes Meyer zum Alten Borgloh
Categories:
 - Utility
 - Archiving
Custom:
  Repo: https://github.com/johanneszab/smscallbackup
PackageIcon: https://github.com/johanneszab/smscallbackup/raw/main/icons/smscallbackup.svg
Screenshots:
 - https://github.com/johanneszab/smscallbackup/raw/main/screenshots/main.png
 - https://github.com/johanneszab/smscallbackup/raw/main/screenshots/restore.png
 - https://github.com/johanneszab/smscallbackup/raw/main/screenshots/restored.png
AIRating: V
AINote: Written with Claude (Anthropic), tested on a Sony Xperia 10 VI with Sailfish OS 5.2
Links:
  Homepage: https://github.com/johanneszab/smscallbackup
  Bugtracker: https://github.com/johanneszab/smscallbackup/issues
%endif

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 VERSION=%{version}
%make_build

%install
%qmake5_install
desktop-file-install --delete-original --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%files
%{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%changelog
* Sun Nov 08 2026 Johannes Meyer zum Alten Borgloh <code@jzab.de> - 0.1.0-1
- First release: back up and restore SMS and the call log in the XML format
  of SMS Backup & Restore, restore compressed backups, skip duplicates.
