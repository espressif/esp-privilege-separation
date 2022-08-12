#!/usr/bin/env python
#
# SPDX-FileCopyrightText: 2016-2022 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import datetime
import hashlib
import operator
import os
import shutil
import struct
import sys
import zlib
from collections import namedtuple
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa, utils
from cryptography.hazmat.primitives import hashes

try:
    import espsecure
except ImportError:  # cheat and use IDF's copy of esptool if available
    idf_path = os.getenv('IDF_PATH')
    if not idf_path or not os.path.exists(idf_path):
        raise
    sys.path.insert(0, os.path.join(idf_path, 'components', 'esptool_py', 'esptool'))
    import espsecure

SIG_BLOCK_MAGIC = 0xB7
SIG_BLOCK_VERSION_RSA = 1
SECTOR_SIZE = 4096

def _load_private_key(key_contents):
    private_key = serialization.load_pem_private_key(key_contents, password=None, backend=default_backend())
    if isinstance(private_key, rsa.RSAPrivateKey):
        if private_key.key_size != 3072:
            print("[-] Key size is %d bits. Secure boot only supports RSA-3072" % private_key.key_size)
            sys.exit(2)
    else:
        print("[-] Key provided is not RSA private key. Secure boot only supports RSA-3072")
        sys.exit(2)

    return private_key

def _load_cert(cert_contents):
    try:
        cert = x509.load_pem_x509_certificate(cert_contents, backend=default_backend())
    except ValueError:
        print("[-] Invalid certificate. Utility expects RSA-3072 certificate")
        sys.exit(2)

    if not isinstance(cert.public_key(), rsa.RSAPublicKey):
        print("[-] Invalid certificate. Utility expects RSA-3072 certificate")
        sys.exit(2)

    if (cert.public_key().key_size != 3072):
        print("[-] Invalid certificate. Key size is %d bits. Utility expects RSA-3072 certificate" % cert.public_key().key_size)
        sys.exit(2)

    if not isinstance(cert.signature_hash_algorithm, hashes.SHA256):
        print("[-] Invalid certificate. Certificate needs to use SHA256 hash algorithm")
        sys.exit(2)

    return cert

def generate_signed_image(app_data, user_app_private_key_data, user_app_cert_data, outfile):
    signature_sector = b""
    contents = app_data
    cert_len = len(user_app_cert_data)

    if cert_len > 3664:
        print("[-] User certificate too long. Size is %d bytes. Max size supported is 3664 bytes" % cert_len)

    if len(contents) % SECTOR_SIZE != 0:
        pad_by = SECTOR_SIZE - (len(contents) % SECTOR_SIZE)
        print(
            "Padding data contents by %d bytes "
            "so signature sector aligns at sector boundary" % pad_by
        )
        contents += b"\xff" * pad_by

    # Calculate digest of data file
    digest = hashlib.sha256()
    digest.update(contents)
    digest = digest.digest()

    private_key = serialization.load_pem_private_key(user_app_private_key_data, password=None, backend=default_backend())
    if isinstance(private_key, rsa.RSAPrivateKey):
        if private_key.key_size != 3072:
            print("[-] Key size is %d bits. Secure boot only supports RSA-3072" % private_key.key_size)
            sys.exit(1)
    else:
        print("[-] Key provided is not RSA private key. Secure boot only supports RSA-3072")
        sys.exit(1)

    # Check if the private key and cert form a correct pair.
    # user_app_cert_data contains an extra NULL byte at the end, exclude it before trying to load the certificate
    cert = _load_cert(user_app_cert_data[:-1])

    if cert.public_key().public_numbers().n != private_key.public_key().public_numbers().n:
        print("[-] Certificate and private key do not match. This certificate does not belong to this private key")
        sys.exit(1)

    # RSA signature
    signature = private_key.sign(
        digest,
        padding.PSS(
            mgf=padding.MGF1(hashes.SHA256()),
            salt_length=32,
        ),
        utils.Prehashed(hashes.SHA256()),)

    # Encode in signature block format
    # signature is kept in big endian format
    signature_block = struct.pack(
        "<BBxx32s384sI",
        SIG_BLOCK_MAGIC,
        SIG_BLOCK_VERSION_RSA,
        digest,
        signature,
        cert_len,
    )

    signature_sector += signature_block + user_app_cert_data

    # Pad signature_sector to sector
    signature_sector = signature_sector + (
        b"\xff" * (SECTOR_SIZE - len(signature_sector) - 8)
    )

    signature_sector += b'\x00' * 4
    signature_sector += struct.pack("<I", zlib.crc32(signature_sector) & 0xFFFFFFFF)

    assert len(signature_sector) == SECTOR_SIZE

    # Write to output file
    with open(outfile, "wb") as f:
        f.write(contents + signature_sector)
    print("[+] Application signed and signature block appended")

def generate_credentials(args, is_ca=False):
    if os.path.exists(args.keyfile):
        print("WARNING: Key file %s already exists. Using it for further operation" % args.keyfile)
        private_key = serialization.load_pem_private_key(
            open(args.keyfile, "rb").read(), password=None, backend=default_backend())

        if isinstance(private_key, rsa.RSAPrivateKey):
            if private_key.key_size != 3072:
                print("[-] Key file has length %d bits. User app secure boot only supports RSA-3072."
                    % private_key.key_size)
        else:
            print("[-] Key file should be RSA-3072.")
    else:
        private_key = rsa.generate_private_key(
            public_exponent=65537, key_size=3072, backend=default_backend()
            )
        with open(args.keyfile, "wb") as f:
            f.write(private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption()
                ))
        print("[+] RSA 3072 private key in PEM format written to %s" % args.keyfile)

    if (is_ca):
        print("Generating CA certificate")
    else:
        print("Generating CSR")

    country = "INVALID"
    while(len(country) != 2):
        if(country != "INVALID"):
            print("[-] Country name should be 2 byte long")
        country = input('Enter Country Name (2 letter code): ')

    state = input("Enter State or province name (full name): ")
    loc = input("Enter Locality name(e.g. city): ")
    org = input("Enter Organization name(e.g. company): ")
    org_unit = input("Enter Organizational Unit Name (e.g. section): ")
    common_name = input("Enter Common name (e.g. fully qualified host name): ")
    email_add = input("Enter email address: ")

    subject = issuer = x509.Name([
    x509.NameAttribute(NameOID.COUNTRY_NAME, country),
    x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, state),
    x509.NameAttribute(NameOID.LOCALITY_NAME, loc),
    x509.NameAttribute(NameOID.ORGANIZATION_NAME, org),
    x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, org_unit),
    x509.NameAttribute(NameOID.COMMON_NAME, common_name),
    x509.NameAttribute(NameOID.EMAIL_ADDRESS, email_add),
    ])

    if (is_ca):
        cert = x509.CertificateBuilder().subject_name(
            subject
        ).issuer_name(
            issuer
        ).public_key(
            private_key.public_key()
        ).serial_number(
            x509.random_serial_number()
        ).not_valid_before(
            datetime.datetime.utcnow()
        ).not_valid_after(
            datetime.datetime.utcnow() + datetime.timedelta(days=365 * 10)
        ).add_extension(
            x509.BasicConstraints(ca=True, path_length=None),
            critical=True
        ).sign(private_key, hashes.SHA256(), backend=default_backend())

        with open(args.certfile, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))
    else:
        csr = x509.CertificateSigningRequestBuilder().subject_name(subject).sign(private_key, hashes.SHA256(), backend=default_backend())
        with open(args.csrfile, "wb") as f:
            f.write(csr.public_bytes(serialization.Encoding.PEM))

def generate_protected_ca(args):
    generate_credentials(args, True)
    print("[+] Protected app certificate written")

def generate_user_credentials(args):
    generate_credentials(args, False)
    print("[+] User app CSR written")

def generate_signed_user_app_cert(args):
    with open(args.protected_ca_cert, "rb") as f:
        cert_contents = f.read()

    cacert = _load_cert(cert_contents)

    with open(args.protected_ca_key, "rb") as f:
        private_key_contents = f.read()

    private_key = _load_private_key(private_key_contents)

    with open(args.user_csr, "rb") as f:
        csr_contents = f.read()

    csr = x509.load_pem_x509_csr(csr_contents)

    user_cert = x509.CertificateBuilder().subject_name(
        csr.subject
    ).issuer_name(
        cacert.subject
    ).public_key(
        csr.public_key()
    ).serial_number(
        x509.random_serial_number()
    ).not_valid_before(
        datetime.datetime.utcnow()
    ).not_valid_after(
        datetime.datetime.utcnow() + datetime.timedelta(days=365 * 10)
    ).add_extension(
        x509.KeyUsage(
            digital_signature=True, key_encipherment=False, content_commitment=False,
            data_encipherment=False, key_agreement=False, encipher_only=False,
            decipher_only=False, key_cert_sign=False, crl_sign=False
        ),
        critical=True
    ).sign(private_key, hashes.SHA256(), backend=default_backend())

    with open(args.user_app_cert, "wb") as f:
        f.write(user_cert.public_bytes(serialization.Encoding.PEM))

    print("[+] Generated signed user app certificate")

def sign_user_app(args):
    with open(args.user_cert, "rb") as f:
        cert_contents = f.read()

        # Verify if the certificate is valid
        cert = _load_cert(cert_contents)

        # mbedTLS APIs require the certificate to end with a NULL character
        cert_contents += b"\x00"

    with open(args.user_app_bin, "rb") as f:
        image_contents = f.read()

    with open(args.user_keyfile, "rb") as f:
        key_contents = f.read()

    generate_signed_image(image_contents, key_contents, cert_contents, args.output)

def main():
    parser = argparse.ArgumentParser(description='esp_ps_secure_boot_util.py: Utility for user app secure boot')

    subparsers = parser.add_subparsers(
        dest="operation", help="Run esp_ps_secure_boot_util.py {command} -h for additional help"
    )

    sub_command = subparsers.add_parser("generate_protected_ca",
        help="Generate protected app private key and certificate based on RSA-3072")

    sub_command.add_argument(
        'keyfile',
        help="Output file for protected app private key"
    )

    sub_command.add_argument(
        'certfile',
        help="Output file for protected app certificate"
    )

    sub_command = subparsers.add_parser("generate_user_credentials",
        help="Generate user app private key and CSR based on RSA-3072")

    sub_command.add_argument(
        'keyfile',
        help="Output file for user app private key"
    )

    sub_command.add_argument(
        'csrfile',
        help="Output file for user app CSR"
    )

    sub_command = subparsers.add_parser("generate_signed_user_app_cert",
        help="Generate signed user app certificate using the protected app CA")

    sub_command.add_argument(
        '--protected_ca_cert',
        help="Protected CA certificate file"
    )

    sub_command.add_argument(
        '--protected_ca_key',
        help="Private key of the protected app"
    )

    sub_command.add_argument(
        '--user_csr',
        help="Certificate Signing Request (CSR) of the user app"
    )

    sub_command.add_argument(
        'user_app_cert',
        help="Output file for user app certificate"
    )

    sub_command = subparsers.add_parser("sign_user_app",
            help="Sign the user application and append the signature block at the end")

    sub_command.add_argument(
        '--user_cert',
        required=True,
        help='Signed user app certificate received from Protected app CA'
    )

    sub_command.add_argument(
        '--user_keyfile',
        required=True,
        help='User app private key'
    )

    sub_command.add_argument(
        '--output',
        "-o",
        required=True,
        help='User app private key'
    )

    sub_command.add_argument(
        'user_app_bin',
        help='Unsigned user app binary file'
    )

    args = parser.parse_args()

    if args.operation is None:
        parser.print_help()
        parser.exit(1)

    # each 'operation' is a module-level function of the same name
    operation_func = globals()[args.operation]
    operation_func(args)

if __name__ == "__main__":
    main()
