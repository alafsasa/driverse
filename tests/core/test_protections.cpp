#include "core/protections.h"

#include <cassert>
#include <iostream>
#include <string>

using driverse::core::Protections;
using driverse::core::SensitiveLevel;

namespace {

void testProtectedDefaults() {
    auto& p = Protections::instance();
    assert(p.isProtected("/etc/passwd"));
    assert(p.isProtected("/usr/bin/ls"));
    assert(p.isProtected("/boot/efi/grub/grub.cfg"));
    // Component boundary: a sibling prefix is not protected.
    assert(!p.isProtected("/etcetera"));
    assert(!p.isProtected("/home/user/.cache"));
    std::cout << "  protected defaults OK\n";
}

void testProtectedRegistryTrailingSlash() {
    auto& p = Protections::instance();

    // Config entries often arrive as "/srv/data/" — the normalized lookup
    // must strip the trailing slash or children stop matching.
    p.addProtectedPath("/opt/driverse_ps/");
    assert(p.isProtected("/opt/driverse_ps"));
    assert(p.isProtected("/opt/driverse_ps/child"));
    assert(p.isProtected("/opt/driverse_ps/child/file.txt"));
    assert(!p.isProtected("/opt/driverse_ps2")); // component boundary

    p.removeProtectedPath("/opt/driverse_ps/");
    assert(!p.isProtected("/opt/driverse_ps/child"));
    std::cout << "  protected registry (trailing slash) OK\n";
}

void testCredentialTier() {
    auto& p = Protections::instance();
    assert(p.sensitiveLevel("/home/u/.ssh") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/.ssh/id_rsa") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/id_ed25519") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/certs/server.pem") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/project/keystore.p12") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/.docker/config.json") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/.aws/credentials") == SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/.local/share/keyrings/login.keyring") ==
           SensitiveLevel::Credential);
    assert(p.sensitiveLevel("/home/u/project/.env") == SensitiveLevel::Credential);
    assert(p.isSensitive("/home/u/.gnupg"));
    std::cout << "  credential tier OK\n";
}

void testPersonalStateTier() {
    auto& p = Protections::instance();
    assert(p.sensitiveLevel("/home/u/.config") == SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/.config/foo/settings.ini") ==
           SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/.local/share/app/data.db") ==
           SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/.local/state/nvim") == SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/.thunderbird/abc.default/mail") ==
           SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/.mozilla/firefox/xyz.default/logins.json") ==
           SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/.var/app/org.test.App/data") ==
           SensitiveLevel::PersonalState);
    assert(p.sensitiveLevel("/home/u/snap/demo/common/notes.txt") ==
           SensitiveLevel::PersonalState);
    std::cout << "  personal-state tier OK\n";
}

void testCacheExemption() {
    auto& p = Protections::instance();
    // Real cache dirs inside profile trees are regenerable by definition.
    assert(p.sensitiveLevel("/home/u/.config/mozilla/xyz.default/cache2/entries/e") ==
           SensitiveLevel::None);
    assert(p.sensitiveLevel("/home/u/.config/chromium/Cache/data_0") ==
           SensitiveLevel::None);
    assert(p.sensitiveLevel("/home/u/.var/app/org.test.App/cache/img.png") ==
           SensitiveLevel::None);
    assert(p.sensitiveLevel("/home/u/snap/demo/5/.cache/x") == SensitiveLevel::None);
    // Credential material still wins over the exemption.
    assert(p.sensitiveLevel("/home/u/.docker/cache/credentials") ==
           SensitiveLevel::Credential);
    std::cout << "  cache exemption OK\n";
}

void testNotSensitive() {
    auto& p = Protections::instance();
    assert(p.sensitiveLevel("/home/u/Downloads/ubuntu.iso") == SensitiveLevel::None);
    assert(p.sensitiveLevel("/home/u/Documents/thesis.pdf") == SensitiveLevel::None);
    assert(p.sensitiveLevel("/home/u/project/build") == SensitiveLevel::None);
    assert(p.sensitiveLevel("/home/u/.cache/thumbnails/normal/x.png") ==
           SensitiveLevel::None);
    // Component boundary: .ssh_backup is not .ssh.
    assert(p.sensitiveLevel("/home/u/.ssh_backup/session.log") == SensitiveLevel::None);
    assert(!p.isSensitive("/home/u/.cache"));
    std::cout << "  non-sensitive paths OK\n";
}

} // namespace

int main() {
    std::cout << "Running protections tests...\n";
    testProtectedDefaults();
    testProtectedRegistryTrailingSlash();
    testCredentialTier();
    testPersonalStateTier();
    testCacheExemption();
    testNotSensitive();
    std::cout << "All protections tests passed!\n";
    return 0;
}
