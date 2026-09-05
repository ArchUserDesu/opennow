"""Back up, stage, verify and install an XEX over FTP. Does not launch it."""
import argparse
import ftplib
import hashlib
import io
import os
from datetime import datetime, timezone
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def retrieve(ftp, name):
    data = io.BytesIO()
    ftp.retrbinary('RETR ' + name, data.write)
    return data.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--user', required=True)
    parser.add_argument('--password', default=os.environ.get('STREAM_FTP_PASSWORD'))
    parser.add_argument('--directory', default='/Hdd1/Homebrew')
    parser.add_argument('--file', default='xdk/bin/Release/default.xex')
    args = parser.parse_args()
    if not args.password:
        parser.error('Set STREAM_FTP_PASSWORD or supply --password')
    binary = Path(args.file)
    data = binary.read_bytes()
    if data[:4] != b'XEX2':
        raise RuntimeError('Refusing an input without an XEX2 header')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup_name = 'default.before-' + stamp + '.xex'
    staged = 'default.' + stamp + '.upload'
    with ftplib.FTP() as ftp:
        ftp.connect(args.host, timeout=30)
        ftp.login(args.user, args.password)
        ftp.cwd(args.directory)
        # A missing or unreadable installed binary stops replacement, leaving
        # the existing title untouched and the error visible to the caller.
        previous = retrieve(ftp, 'default.xex')
        backup = binary.parent / backup_name
        backup.write_bytes(previous)
        print('Local backup:', backup, 'SHA256:', digest(previous))
        ftp.storbinary('STOR ' + staged, io.BytesIO(data))
        if digest(retrieve(ftp, staged)) != digest(data):
            raise RuntimeError('Staged upload hash mismatch; installed XEX unchanged')
        ftp.rename('default.xex', backup_name)
        try:
            ftp.rename(staged, 'default.xex')
            if digest(retrieve(ftp, 'default.xex')) != digest(data):
                raise RuntimeError('Installed XEX hash mismatch')
        except Exception:
            # Keep the failed artifact for inspection; restore the old name.
            try:
                ftp.rename('default.xex', 'default.failed-' + stamp + '.xex')
            except ftplib.error_perm:
                pass
            ftp.rename(backup_name, 'default.xex')
            raise
        print('DEPLOYED:', args.host + ':' + args.directory + '/default.xex')
        print('Bytes:', len(data), 'SHA256:', digest(data))
        print('Remote backup:', backup_name)


if __name__ == '__main__':
    main()
