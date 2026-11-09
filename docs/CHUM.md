<!-- SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh -->
<!-- SPDX-License-Identifier: MIT -->

# Publishing in SailfishOS:Chum

[SailfishOS:Chum](https://github.com/sailfishos-chum/main) packages are built on the
Sailfish OS OBS at [build.sailfishos.org](https://build.sailfishos.org) from a git
repository. The spec file is `rpm/smscallbackup.spec`; its `%description` ends with the
Chum metadata block (title, categories, icon, screenshots, links).

## First release

1. **Publish the source.** Create the GitHub repository `johanneszab/smscallbackup`
   (the URLs in the spec and in the About page point there), push `main` and tag the
   release with the version from the spec:

   ```sh
   git tag -a 0.1.0 -m "SMS & Call Backup 0.1.0"
   git push origin main --tags
   ```

2. **OBS account.** Request one as described on the front page of build.sailfishos.org.
   (Without one, the Chum maintainers can add the package for you: open an issue in
   [sailfishos-chum/main](https://github.com/sailfishos-chum/main/issues).)

3. **Build in your home project.** In `home:<user>` create the package `smscallbackup`
   with this `_service` file:

   ```xml
   <services>
     <service name="tar_git">
       <param name="url">https://github.com/johanneszab/smscallbackup.git</param>
       <param name="branch">main</param>
       <param name="revision">0.1.0</param>
       <param name="token"/>
       <param name="debian">N</param>
       <param name="dumb">N</param>
     </service>
   </services>
   ```

   Add the Sailfish OS release repositories you want to support (the same ones
   `sailfishos:chum` builds for; the app needs Sailfish OS 4.5 or newer for Sailjail and
   the pickers) for aarch64, armv7hl and i486, and check that all builds succeed.
   The build needs `libcommhistory-qt5-devel`, which comes from the Sailfish OS release
   repositories, so no extra dependencies have to be submitted.

4. **Submit.** Use "Submit package" with the target project `sailfishos:chum:testing`.
   The Chum testing maintainers review it and, if it builds and works, promote it to
   `sailfishos:chum`. Then it shows up in the Chum GUI app.

## Updates

1. Raise `Version` in the spec, add an entry to `%changelog` (newest first), commit, tag
   with the new version and push the tag.
2. In `sailfishos:chum:testing`, change `revision` in the package's `_service` to the new
   tag. OBS rebuilds the package.
3. When the builds succeed and the update works, "Submit package" to `sailfishos:chum`
   (leave the target package name empty).

## Before each release

* Unit tests on a PC: `qmake6 tests/tests.pro && make && ./tst_core` (all pass).
* Build with the Sailfish SDK (`mb2 build`) and install the RPM on a phone.
* On the phone: back up, restore a backup of SMS Backup & Restore (Android) with MMS,
  restore the same backup again (nothing must be added the second time), check the
  Messages and Phone apps (MMS pictures, group conversations).
* Incremental backup twice in a row: the second one adds nothing and leaves `sms.xml` and
  `calls.xml` untouched.
* Automatic backup: set a time a few minutes ahead and check that
  `systemctl --user list-timers` shows `smscallbackup-scheduled.timer` and that the
  backup runs (`journalctl --user -u smscallbackup-scheduled`).
* Translations: the build updates `translations/*.ts` with lupdate; run
  `python3 tools/translations.py` and add the German texts it reports as missing.
* Screenshots in `screenshots/` must not show real names or numbers.
