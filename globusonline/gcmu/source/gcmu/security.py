import os
import pkgutil
from subprocess import Popen, PIPE

GCMU_DEFAULT_CADIR = os.path.join(
                    'etc', 'grid-security', 'gcmu', 'certificates')

def install_ca(cadir = None, ca_cert = None, ca_signing_policy = None):
    """
    Installs the go-ca-cert and signing policy from the GCMU package into
    the specified cadir. If cadir is not passed in, the default 
    /etc/grid-security/gcmu/certificates is used
    """
    if cadir == None:
        cadir = GCMU_DEFAULT_CADIR

    if ca_cert is None:
        ca_cert = pkgutil.get_data("gcmu", "go-ca-cert.pem")
    if ca_signing_policy is None:
        ca_signing_policy = pkgutil.get_data(
                "gcmu", "go-ca-cert.signing_policy")
    ca_hash = get_certificate_hash_from_data(ca_cert)

    try:
        old_umask = os.umask(0133)

        go_ca_certfile = open(os.path.join(cadir, ca_hash+'.0'), "w")
        try:
            go_ca_certfile.write(ca_cert)
        finally:
            go_ca_certfile.close()

        go_ca_signing_file = open(
                os.path.join(cadir, ca_hash+".signing_policy"), "w")
        try:
            go_ca_signing_file.write(ca_signing_policy)
        finally:
            go_ca_signing_file.close()
    finally:
        os.umask(old_umask)

def get_certificate_subject(cert_file_path, nameopt=''):
    """
    Parse the X.509 certificate located at cert_file_path and return
    a string containing the Subject DN of the certificate
    """
    args = [ 'openssl', 'x509', '-subject', '-in', cert_file_path, '-noout' ]
    if nameopt != '':
        args.append('-nameopt')
        args.append(nameopt)
    proc = Popen(args, stdout = PIPE, stderr = PIPE)
    (out, err) = proc.communicate()
    returncode = proc.returncode

    if returncode != 0:
        raise Exception("Error " + str(returncode) +
            " getting certificate subject from " +
            cert_file_path + "\n" + err)
    subject = out[9:].strip()

    return subject

def get_certificate_hash(cert_file_path):
    args = [ 'openssl', 'x509', '-hash', '-in', cert_file_path, '-noout' ]
    proc = Popen(args, stdout = PIPE, stderr = PIPE)
    (out, err) = proc.communicate()
    returncode = proc.returncode

    if returncode != 0:
        raise Exception("Error " + str(returncode) +
            " getting certificate subject from " +
            cert_file_path + "\n" + err)
    hashval = out.strip()

    return hashval

def get_certificate_hash_from_data(cert_data):
    args = [ 'openssl', 'x509', '-hash', '-noout' ]
    proc = Popen(args, stdin = PIPE, stdout = PIPE, stderr = PIPE)
    (out, err) = proc.communicate(cert_data)
    returncode = proc.returncode

    if returncode != 0:
        raise Exception("Error " + str(returncode) +
            " getting certificate subject from " +
            cert_file_path + "\n" + err)
    hashval = out.strip()

    return hashval