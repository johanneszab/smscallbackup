#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
# SPDX-License-Identifier: MIT
"""Fills translations/smscallbackup-de.ts and -en.ts from the tables below.

The build updates the .ts files with lupdate; run this afterwards to fill in new strings
(python3 tools/translations.py). It lists strings that have no translation yet.
"""
import os
import sys
import xml.etree.ElementTree as ET

PLURAL_EN = {
    "%n message(s)": ["%n message", "%n messages"],
    "%n call(s)": ["%n call", "%n calls"],
    "%n SMS": ["%n SMS", "%n SMS"],
    "%n MMS": ["%n MMS", "%n MMS"],
    "%n attachment(s) could not be read and are missing in the backup.": [
        "%n attachment could not be read and is missing in the backup.",
        "%n attachments could not be read and are missing in the backup."],
    "%n attachment(s) of MMS are not in the backup (it was made without them). Their messages are restored without them.": [
        "%n attachment of an MMS is not in the backup (it was made without attachments). Its message is restored without it.",
        "%n attachments of MMS are not in the backup (it was made without attachments). Their messages are restored without them."],
}

PLURAL_DE = {
    "%n message(s)": ["%n Nachricht", "%n Nachrichten"],
    "%n call(s)": ["%n Anruf", "%n Anrufe"],
    "%n SMS": ["%n SMS", "%n SMS"],
    "%n MMS": ["%n MMS", "%n MMS"],
    "%n attachment(s) could not be read and are missing in the backup.": [
        "%n Anhang konnte nicht gelesen werden und fehlt in der Sicherung.",
        "%n Anhänge konnten nicht gelesen werden und fehlen in der Sicherung."],
    "%n attachment(s) of MMS are not in the backup (it was made without them). Their messages are restored without them.": [
        "%n Anhang einer MMS ist nicht in der Sicherung (sie wurde ohne Anhänge gemacht). Die Nachricht wird ohne ihn wiederhergestellt.",
        "%n Anhänge von MMS sind nicht in der Sicherung (sie wurde ohne Anhänge gemacht). Die Nachrichten werden ohne sie wiederhergestellt."],
}

DE = {
    # app, main page, cover
    "SMS & Call Backup": "SMS & Call Backup",
    "About": "Über",
    "Version %1": "Version %1",
    "Restore from files": "Aus Dateien wiederherstellen",
    "Cancel backup": "Sicherung abbrechen",
    "Back up now": "Jetzt sichern",
    "Back up": "Sichern",
    "On this phone: %1 and %2.": "Auf diesem Telefon: %1 und %2.",
    "Messages": "Nachrichten",
    "Call log": "Anrufliste",
    "Saved in calls-<date>.xml": "Gespeichert in calls-<Datum>.xml",
    "Contact names": "Kontaktnamen",
    "Adds the contact's name to each entry, as the Android app does. Only for reading the file; restoring does not need them.":
        "Schreibt wie die Android-App den Namen des Kontakts zu jedem Eintrag. Nur zum Lesen der Datei, zum Wiederherstellen werden die Namen nicht gebraucht.",
    "Folder": "Ordner",
    "Cancel": "Abbrechen",
    "Last backup: %1": "Letzte Sicherung: %1",
    "Restore": "Wiederherstellen",
    "Tap a backup to see what it contains. Only what is not on the phone yet is restored.":
        "Eine Sicherung antippen, um ihren Inhalt zu sehen. Wiederhergestellt wird nur, was noch nicht auf dem Telefon ist.",
    "No backups in this folder yet. Backups of SMS Backup & Restore on Android work too: copy their XML files into this folder, or choose them with “Restore from files” in the pull-down menu.":
        "Noch keine Sicherungen in diesem Ordner. Sicherungen von SMS Backup & Restore unter Android gehen auch: deren XML-Dateien in diesen Ordner kopieren oder mit „Aus Dateien wiederherstellen“ im Aufziehmenü auswählen.",
    "Delete": "Löschen",
    "Deleting": "Wird gelöscht",
    "Backup folder": "Ordner für Sicherungen",
    "Backup files": "Sicherungsdateien",
    "Saved %1 and %2.": "%1 und %2 gespeichert.",
    "On this phone: %1, %2 and %3.": "Auf diesem Telefon: %1, %2 und %3.",
    "SMS and MMS, saved in sms.xml": "SMS und MMS, gespeichert in sms.xml",
    "SMS and MMS, saved in sms-<date>.xml": "SMS und MMS, gespeichert in sms-<Datum>.xml",
    "Saved in calls.xml": "Gespeichert in calls.xml",
    "Add to the same files": "Zu denselben Dateien hinzufügen",
    "Each backup adds what is new to sms.xml and calls.xml. They keep everything, also what was deleted on the phone.":
        "Jede Sicherung fügt Neues zu sms.xml und calls.xml hinzu. Sie behalten alles, auch was auf dem Telefon gelöscht wurde.",
    "Each backup writes new files with everything that is on the phone.":
        "Jede Sicherung schreibt neue Dateien mit allem, was auf dem Telefon ist.",
    "Automatic backup": "Automatische Sicherung",
    "The automatic backup of %1 failed: %2": "Die automatische Sicherung vom %1 ist fehlgeschlagen: %2",
    "each backup adds to them": "jede Sicherung ergänzt sie",
    "Nothing new since the last backup.": "Nichts Neues seit der letzten Sicherung.",
    "Added %1 and %2.": "%1 und %2 hinzugefügt.",
    "Daily at %1": "Täglich um %1",
    "Every %1 at %2": "Jeden %1 um %2",
    "Monthly on day %1 at %2": "Monatlich am %1. um %2",
    "Off": "Aus",
    # automatic backup page
    "Never": "Nie",
    "Every day": "Jeden Tag",
    "Every week": "Jede Woche",
    "Every month": "Jeden Monat",
    "Day": "Tag",
    "Day of the month": "Tag im Monat",
    "In months without day %1, on their last day.": "In Monaten ohne %1. Tag am letzten Tag des Monats.",
    "Time": "Uhrzeit",
    "Next backup: as soon as possible": "Nächste Sicherung: so bald wie möglich",
    "Next backup: %1": "Nächste Sicherung: %1",
    "The backup runs in the background, also when the app is closed, and uses the choices of the main page: what to back up, the folder and whether to add to the same files. It starts within 15 minutes of the time set, or, if the phone is switched off or asleep then, as soon as it is on again. If it fails, a notification tells why.":
        "Die Sicherung läuft im Hintergrund, auch wenn die App geschlossen ist, mit den Einstellungen der Hauptseite: was gesichert wird, der Ordner und ob zu denselben Dateien hinzugefügt wird. Sie beginnt bis zu 15 Minuten nach der eingestellten Zeit oder, wenn das Telefon dann ausgeschaltet ist oder schläft, sobald es wieder an ist. Schlägt sie fehl, sagt eine Benachrichtigung warum.",
    "Last automatic backup": "Letzte automatische Sicherung",
    "When": "Wann",
    "Automatic backup failed": "Automatische Sicherung fehlgeschlagen",
    "Backing up %1%": "Sicherung %1 %",
    "Restoring %1%": "Wiederherstellung %1 %",
    # restore page
    "Left out: %1 %2": "Ausgelassen: %1 %2",
    "Restored %1 and %2.": "%1 und %2 wiederhergestellt.",
    "Messages in the backup": "Nachrichten in der Sicherung",
    "MMS among them": "Davon MMS",
    "New for this phone": "Neu für dieses Telefon",
    "Calls in the backup": "Anrufe in der Sicherung",
    "From": "Von",
    "To": "Bis",
    "SIM": "SIM",
    "The messages and calls are filed under this SIM.": "Die Nachrichten und Anrufe werden dieser SIM zugeordnet.",
    "Mark messages as read": "Nachrichten als gelesen markieren",
    "Otherwise messages that were unread in the backup show up as new.":
        "Sonst erscheinen Nachrichten, die in der Sicherung ungelesen sind, als neu.",
    "Restoring": "Wird wiederhergestellt",
    "Everything in this backup is already on the phone.": "Alles aus dieser Sicherung ist schon auf dem Telefon.",
    "Please keep the app open until the restore is finished.":
        "Bitte die App geöffnet lassen, bis die Wiederherstellung fertig ist.",
    "They are in the Messages and Phone apps now.": "Sie sind jetzt in den Apps Nachrichten und Telefon.",
    # about page
    "Backs up your SMS, MMS and call log in the XML format of the Android app SMS Backup & Restore, and restores such backups, also their RCS chats. Move your history between Android and Sailfish OS in both directions, or keep a copy of it, by hand or automatically.":
        "Sichert SMS, MMS und Anrufliste im XML-Format der Android-App SMS Backup & Restore und stellt solche Sicherungen wieder her, auch ihre RCS-Chats. So zieht der Verlauf in beide Richtungen zwischen Android und Sailfish OS um, oder es bleibt eine Kopie, von Hand oder automatisch.",
    "Restoring only adds what is not on the phone yet, so restoring a backup twice does no harm. Restored messages go into the existing conversations of the Messages app.":
        "Beim Wiederherstellen kommt nur hinzu, was noch nicht auf dem Telefon ist. Eine Sicherung zweimal wiederherzustellen schadet daher nicht. Wiederhergestellte Nachrichten landen in den vorhandenen Unterhaltungen der Nachrichten-App.",
    "Not affiliated with SyncTech, the maker of SMS Backup & Restore. Its file format is documented on synctech.com.au.":
        "Keine Verbindung zu SyncTech, dem Hersteller von SMS Backup & Restore. Das Dateiformat ist auf synctech.com.au dokumentiert.",
    "Command line": "Kommandozeile",
    "License": "Lizenz",
    "Copyright © 2026 Johannes Meyer zum Alten Borgloh. Free software under the MIT license.":
        "Copyright © 2026 Johannes Meyer zum Alten Borgloh. Freie Software unter der MIT-Lizenz.",
    "Source code": "Quellcode",
    # backup task
    "Choose what to back up.": "Bitte auswählen, was gesichert werden soll.",
    "Reading the history": "Verlauf wird gelesen",
    "Backup cancelled.": "Sicherung abgebrochen.",
    "Looking up contact names": "Kontaktnamen werden gesucht",
    "Writing the backup": "Sicherung wird geschrieben",
    "Cannot create the folder %1.": "Der Ordner %1 kann nicht angelegt werden.",
    "Cannot write %1: %2": "%1 kann nicht geschrieben werden: %2",
    "Writing %1 failed: %2": "Schreiben von %1 fehlgeschlagen: %2",
    "Another backup is running at the moment. Please try again in a minute.":
        "Gerade läuft schon eine Sicherung. Bitte in einer Minute noch einmal versuchen.",
    "%1 cannot be read, so it was left as it is: %2 Move it away to start a new one.":
        "%1 kann nicht gelesen werden und wurde deshalb nicht verändert: %2 Für eine neue Datei die alte woanders hin verschieben.",
    "%1 is not a backup of %2, so it was left as it is. Move it away to start a new one.":
        "%1 ist keine Sicherung von %2 und wurde deshalb nicht verändert. Für eine neue Datei die alte woanders hin verschieben.",
    "messages": "Nachrichten",
    "calls": "Anrufen",
    "%1 cannot be read, so it was left as it is: %2": "%1 kann nicht gelesen werden und wurde deshalb nicht verändert: %2",
    "%1 was changed during the backup, so it was left as it is. Please try again.":
        "%1 wurde während der Sicherung verändert und deshalb nicht angefasst. Bitte noch einmal versuchen.",
    "Storing the attachment %1 failed.": "Der Anhang %1 konnte nicht gespeichert werden.",
    # restore task
    "No backup file selected.": "Keine Sicherungsdatei ausgewählt.",
    "Reading the backup": "Sicherung wird gelesen",
    "Reading %1": "%1 wird gelesen",
    "Reading the history on the phone": "Verlauf auf dem Telefon wird gelesen",
    "Storing the attachments of the backup failed: %1": "Die Anhänge der Sicherung konnten nicht gespeichert werden: %1",
    # database
    "There is no message database at %1.": "Unter %1 gibt es keine Nachrichtendatenbank.",
    "Cannot open the message database: %1": "Die Nachrichtendatenbank lässt sich nicht öffnen: %1",
    "The message database has an unexpected layout: %1": "Die Nachrichtendatenbank hat einen unerwarteten Aufbau: %1",
    "Reading the conversations failed: %1": "Lesen der Unterhaltungen fehlgeschlagen: %1",
    "Reading the message database failed: %1": "Lesen der Nachrichtendatenbank fehlgeschlagen: %1",
    "Adding the conversations to the message database failed.":
        "Die Unterhaltungen konnten nicht in die Nachrichtendatenbank eingetragen werden.",
    "Cancelled.": "Abgebrochen.",
    "No conversation for %1.": "Keine Unterhaltung für %1.",
    "Writing to the message database failed.": "Schreiben in die Nachrichtendatenbank fehlgeschlagen.",
    # mapping
    "drafts": "Entwürfe",
    "unsent or failed messages": "nicht gesendete oder fehlgeschlagene Nachrichten",
    "entries without a phone number": "Einträge ohne Telefonnummer",
    "entries without a date": "Einträge ohne Datum",
    "entries of an unknown type": "Einträge unbekannter Art",
    "MMS notifications and reports": "MMS-Benachrichtigungen und -Berichte",
    "MMS without text or attachments": "MMS ohne Text und Anhänge",
    # files
    "Cannot open %1: %2": "%1 lässt sich nicht öffnen: %2",
    "%1 is not a backup of SMS Backup & Restore.": "%1 ist keine Sicherung von SMS Backup & Restore.",
    "%1 is damaged: %2": "%1 ist beschädigt: %2",
    "%1 is not a valid backup file (line %2: %3).": "%1 ist keine gültige Sicherungsdatei (Zeile %2: %3).",
    "Writing the backup failed.": "Schreiben der Sicherung fehlgeschlagen.",
    "SIM %1": "SIM %1",
    # zip
    "Cannot initialize decompression.": "Entpacken kann nicht gestartet werden.",
    "The file is not a zip archive.": "Die Datei ist kein Zip-Archiv.",
    "The zip archive is damaged (no central directory).": "Das Zip-Archiv ist beschädigt (kein zentrales Verzeichnis).",
    "The zip archive is damaged (ZIP64 locator missing).": "Das Zip-Archiv ist beschädigt (ZIP64-Locator fehlt).",
    "The zip archive is damaged.": "Das Zip-Archiv ist beschädigt.",
    "The zip archive is damaged (ZIP64 record missing).": "Das Zip-Archiv ist beschädigt (ZIP64-Eintrag fehlt).",
    "The zip archive is empty.": "Das Zip-Archiv ist leer.",
    "The zip archive is damaged (bad central directory).": "Das Zip-Archiv ist beschädigt (ungültiges zentrales Verzeichnis).",
    "The zip archive is damaged (truncated).": "Das Zip-Archiv ist beschädigt (unvollständig).",
    "The zip archive is damaged (bad directory entry).": "Das Zip-Archiv ist beschädigt (ungültiger Verzeichniseintrag).",
    "The zip archive contains no file.": "Das Zip-Archiv enthält keine Datei.",
    "The zip archive is password-protected. Unzip it on a computer and restore the .xml file inside.":
        "Das Zip-Archiv ist mit einem Passwort geschützt. Bitte am Computer entpacken und die .xml-Datei darin wiederherstellen.",
    "The zip archive uses an unsupported compression method (%1).":
        "Das Zip-Archiv verwendet eine nicht unterstützte Kompression (%1).",
    "The zip archive is damaged (bad file header).": "Das Zip-Archiv ist beschädigt (ungültiger Dateikopf).",
    "The zip archive is truncated.": "Das Zip-Archiv ist unvollständig.",
    "The zip archive is damaged (data ends early).": "Das Zip-Archiv ist beschädigt (Daten enden zu früh).",
    "The zip archive is damaged (%1).": "Das Zip-Archiv ist beschädigt (%1).",
    "The zip archive is damaged (checksum mismatch).": "Das Zip-Archiv ist beschädigt (Prüfsumme stimmt nicht).",
}


def fill(path, singular, plural):
    tree = ET.parse(path)
    missing = []
    for message in tree.getroot().iter("message"):
        source = message.find("source").text or ""
        translation = message.find("translation")
        if translation is None:
            translation = ET.SubElement(message, "translation")
        numerus = message.get("numerus") == "yes"
        if numerus:
            forms = plural.get(source)
            if not forms:
                missing.append(source)
                continue
            for child in list(translation):
                translation.remove(child)
            translation.text = None
            for form in forms:
                ET.SubElement(translation, "numerusform").text = form
            translation.attrib.pop("type", None)
        elif singular is not None:
            text = singular.get(source)
            if text is None:
                missing.append(source)
                continue
            translation.text = text
            translation.attrib.pop("type", None)
    ET.indent(tree, space="    ")
    body = ET.tostring(tree.getroot(), encoding="unicode")
    with open(path, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + body + "\n")
    return missing


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "translations")
    missing = fill(os.path.join(root, "smscallbackup-de.ts"), DE, PLURAL_DE)
    missing_en = fill(os.path.join(root, "smscallbackup-en.ts"), None, PLURAL_EN)
    for source in sorted(set(missing)):
        print("de: no translation for %r" % source)
    for source in sorted(set(missing_en)):
        print("en: no plural forms for %r" % source)
    return 1 if missing or missing_en else 0


if __name__ == "__main__":
    sys.exit(main())
