#pragma once

#include <string>

namespace pgstream
{
class CertificateManager
{
public:
    static std::string certificatePem();
    static std::string privateKeyPem();
    static std::string certificateSummary();
    static int installIntoSslContext(void* sslContext);
};
}
