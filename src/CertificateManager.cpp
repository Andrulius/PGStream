#include "CertificateManager.h"
#include <BinaryData.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

namespace pgstream
{
std::string CertificateManager::certificatePem()
{
    return { PGStreamBinaryData::devcert_pem, static_cast<size_t> (PGStreamBinaryData::devcert_pemSize) };
}

std::string CertificateManager::privateKeyPem()
{
    return { PGStreamBinaryData::devkey_pem, static_cast<size_t> (PGStreamBinaryData::devkey_pemSize) };
}

std::string CertificateManager::certificateSummary()
{
    return "Embedded self-signed development certificate; browser trust must be accepted manually.";
}

int CertificateManager::installIntoSslContext(void* sslContext)
{
    auto* ctx = static_cast<SSL_CTX*> (sslContext);
    if (ctx == nullptr)
        return -1;

    const auto cert = certificatePem();
    const auto key = privateKeyPem();

    BIO* certBio = BIO_new_mem_buf(cert.data(), static_cast<int> (cert.size()));
    BIO* keyBio = BIO_new_mem_buf(key.data(), static_cast<int> (key.size()));
    if (certBio == nullptr || keyBio == nullptr)
    {
        if (certBio != nullptr)
            BIO_free(certBio);
        if (keyBio != nullptr)
            BIO_free(keyBio);
        return -1;
    }

    X509* x509 = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
    BIO_free(certBio);
    BIO_free(keyBio);

    if (x509 == nullptr || pkey == nullptr)
    {
        if (x509 != nullptr)
            X509_free(x509);
        if (pkey != nullptr)
            EVP_PKEY_free(pkey);
        return -1;
    }

    const auto ok = SSL_CTX_use_certificate(ctx, x509) == 1
        && SSL_CTX_use_PrivateKey(ctx, pkey) == 1
        && SSL_CTX_check_private_key(ctx) == 1;

    X509_free(x509);
    EVP_PKEY_free(pkey);

    return ok ? 1 : -1;
}
}
