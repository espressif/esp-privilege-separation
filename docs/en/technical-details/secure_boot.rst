Secure boot
===========

Overview
--------

Secure boot is a feature that guarantees that only trusted application (an application that contains a valid digital signature) can run on the device.

In this framework, there are two separate applications, protected_app and user_app.

Both these applications should be able to boot securely and for that we have made provision to enable secure
boot for both these applications.

Protected app secure boot
-------------------------

Protected application is similar to the application in ESP-IDF. Therefore, it follows the Secure boot mechanism of ESP-IDF application.

Please refer to `Secure Boot V2 <https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32c3/security/secure-boot-v2.html>`_ to
enable secure boot for protected application


User app secure boot
--------------------

User application is verified and loaded by the protected application.
The protected application is already verified and loaded by the 2nd stage bootloader so the root of trust is already established.

For protected app to verify if the user app is authorized by the appropriate entity, it needs to have certain knowledge.

We have chosen a certificate based verification scheme to verify the authenticity of the user app

Certificate based verification scheme
-------------------------------------

In this scheme, there are certain pre-requisites on the protected app and user app side.

Protected app
~~~~~~~~~~~~~

1. Owner of the protected app is required to have a CA (Certificate Authority).
   It should provide a service to sign and generate certificates for user application using the user app CSR (Certificate Signing Request).

2. The CA certified needs to be included into the protected application.
   This certificate will be used to verify the user app certificate (UAC).

User app
~~~~~~~~

1. Owner of the user app is required to generate a RSA3072 key pair (private and public key).

2. Owner of the user app is required to generate a CSR from the private key. This CSR then needs to be signed by the protected app owner with their CA key and certificate to generate the user app certificate (UAC).

3. User app should be signed using the same private key generated in step 1. The signature should be appended at the end of the user app firmware.

4. The user app certificate (UAC) signed by the protected app CA certificate should also be appended to the user app firmware. The format is defined in :ref:`signature_block_format`

Verification process
--------------------

Verification process till protected application is the same as ESP-IDF `Secure Boot process <https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32c3/security/secure-boot-v2.html#secure-boot-v2-process>`_

1. Once the protected application starts executing, a root of trust is established and protected app is guaranteed to be an authorized code
2. Protected application looks for a valid user app in the flash and if found, it looks for the UAC at the end. The certificate is then verified with the CA certificate already present in the protected app. If the certificate verification fails, user app boot is aborted.
3. After the certificate is verified, protected app looks for the signature in the user app. This RSA-PSS signature is validated using the public key obtained from the certificate. If the signature is invalid, user app boot is aborted.
4. Protected application then loads and executes the trusted user application.


.. _signature_block_format:

Signature block format
----------------------

The content of signature block is shown in the following table:

.. list-table:: Content of a certificate Block
    :widths: 10 10 40
    :header-rows: 1

    * - **Offset**
      - **Size (bytes)**
      - **Description**
    * - 0
      - 1
      - Magic byte
    * - 1
      - 1
      - Version number byte (currently 0x01)
    * - 2
      - 2
      - Padding bytes, Reserved. Should be zero.
    * - 4
      - 32
      - Digest of the application
    * - 36
      - 384
      - RSA-PSS Signature result (section 8.1.1 of RFC8017) of image content, computed using following PSS parameters: SHA256 hash, MFG1 function, 0 length salt, default trailer field (0xBC).
    * - 420
      - 4
      - Length of the user app certificate (UAC)
    * - 424
      - 3664
      - Contents of the entire user app certificate (starting with `-----BEGIN CERTIFICATE-----` and ending with `-----END CERTIFICATE-----`) followed by a NULL character and then padded with 0xFF till 3664 byte length.
    * - 4088
      - 4
      - Zero padding to achieve 4092 length
    * - 4092
      - 4
      - CRC of the preceding 4092 bytes

How to enable Secure boot
-------------------------

We have provided a python utility that can help generate required credentials and also sign application. It can be found here :component_file:`esp_ps_secure_boot_util.py<../tools/esp_ps_secure_boot_util.py>`.

Install the required dependencies and add the directory to the PATH environment variable.

::

    $ pip install -r /path/to/esp-privilege-separation/tools/requirements.txt

    $ export PATH=$PATH:/path/to/esp-privilege-separation/tools


Protected app
~~~~~~~~~~~~~

- To enable secure boot for protected app, follow the `guide in ESP-IDF documentation <https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32c3/security/secure-boot-v2.html#how-to-enable-secure-boot-v2>`_

- To enable secure boot for user app,

    1. Open project configuration menu (``idf.py menuconfig``), in ``Privilege Separation`` under ``Security features``, select ``Enable user app secure boot``.

    2. Specify the path to the protected app CA certificate, relative to the project directory.
       This CA certificate will be used by the protected app to verify the user app.


**To generate CA**

Generate a private key and it's corresponding certificate on your device. This key needs to be protected at all costs

::

    esp_ps_secure_boot_util.py generate_protected_ca protected_priv_key.pem protected_ca_cert.pem

Place the generated CA certificate at the location specified in the menuconfig above.

**To generate user app certificate (UAC) from the CSR**

::

    esp_ps_secure_boot_util.py generate_signed_user_app_cert --protected_ca_key protected_priv_key.pem --protected_ca_cert protected_ca_cert.pem --user_csr user_app_csr.pem user_app_cert.pem

User app
~~~~~~~~

1. Make sure ``Enable user app secure boot`` is selected from the menuconfig.

2. Generate a user app private key and a corresponding CSR. This key needs to be protected at all costs.
   ::

    esp_ps_secure_boot_util.py generate_user_credentials user_app_priv_key.pem user_app_csr.pem

   This CSR needs to be sent to the protected app owners, they will sign it using the CA and generate a user app certificate (UAC)
   ``user_app_cert.pem``.

3. Build the user application.

4. Once the user app is built, you will have to sign and append the user app certificate.
   ::

    esp_ps_secure_boot_util.py sign_user_app --user_cert user_app_cert.pem --user_keyfile user_app_priv_key.pem -o <path-to-project>/build/user_app/user_app_signed.bin <path-to-project>/build/user_app/user_app.bin

6. Flash the application manually using esptool command displayed on the console after building the user application.
