// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/public/doh_provider_list.h"

#include "base/logging.h"
#include "base/no_destructor.h"

namespace net {

DohProviderEntry::DohProviderEntry(std::string provider,
                                   std::set<std::string> ip_strs,
                                   std::set<std::string> dns_over_tls_hostnames,
                                   std::string dns_over_https_template,
                                   std::string ui_name,
                                   std::string privacy_policy,
                                   bool display_globally,
                                   std::set<std::string> display_countries)
    : provider(std::move(provider)),
      dns_over_tls_hostnames(std::move(dns_over_tls_hostnames)),
      dns_over_https_template(std::move(dns_over_https_template)),
      ui_name(std::move(ui_name)),
      privacy_policy(std::move(privacy_policy)),
      display_globally(display_globally),
      display_countries(std::move(display_countries)) {
  DCHECK(!display_globally || display_countries.empty());
  for (const auto& display_country : display_countries) {
    DCHECK_EQ(2u, display_country.size());
  }
  for (const std::string& ip_str : ip_strs) {
    IPAddress ip_address;
    bool success = ip_address.AssignFromIPLiteral(ip_str);
    DCHECK(success);
    ip_addresses.insert(ip_address);
  }
}

DohProviderEntry::DohProviderEntry(const DohProviderEntry& other) = default;
DohProviderEntry::~DohProviderEntry() = default;

const std::vector<DohProviderEntry>& GetDohProviderList() {
  // The provider names in these entries should be kept in sync with the
  // DohProviderId histogram suffix list in
  // tools/metrics/histograms/histograms.xml.
  static const base::NoDestructor<std::vector<DohProviderEntry>> providers{{
      DohProviderEntry(
          "CleanBrowsingAdult",
          {"185.228.168.10", "185.228.169.11", "2a0d:2a00:1::1",
           "2a0d:2a00:2::1"},
          {"adult-filter-dns.cleanbrowsing.org"} /* dot_hostnames */,
          "https://doh.cleanbrowsing.org/doh/adult-filter{?dns}",
          "" /* ui_name */, "" /* privacy_policy */,
          false /* display_globally */, {} /* display_countries */),
      DohProviderEntry(
          "CleanBrowsingFamily",
          {"185.228.168.168", "185.228.169.168",
           "2a0d:2a00:1::", "2a0d:2a00:2::"},
          {"family-filter-dns.cleanbrowsing.org"} /* dot_hostnames */,
          "https://doh.cleanbrowsing.org/doh/family-filter{?dns}",
          "CleanBrowsing family filter" /* ui_name */,
          "https://cleanbrowsing.org/privacy" /* privacy_policy */,
          true /* display_globally */, {} /* display_countries */),
      DohProviderEntry(
          "CleanBrowsingSecure",
          {"185.228.168.9", "185.228.169.9", "2a0d:2a00:1::2",
           "2a0d:2a00:2::2"},
          {"security-filter-dns.cleanbrowsing.org"} /* dot_hostnames */,
          "https://doh.cleanbrowsing.org/doh/security-filter{?dns}",
          "" /* ui_name */, "" /* privacy_policy */,
          false /* display_globally */, {} /* display_countries */),
      DohProviderEntry(
          "Cloudflare",
          {"1.1.1.1", "1.0.0.1", "2606:4700:4700::1111",
           "2606:4700:4700::1001"},
          {"one.one.one.one",
           "1dot1dot1dot1.cloudflare-dns.com"} /* dns_over_tls_hostnames */,
          "https://chrome.cloudflare-dns.com/dns-query",
          "Cloudflare" /* ui_name */,
          "https://developers.cloudflare.com/1.1.1.1/commitment-to-privacy/"
          "privacy-policy/privacy-policy/" /* privacy_policy */,
          true /* display_globally */, {} /* display_countries */),
      DohProviderEntry("Comcast",
                       {"75.75.75.75", "75.75.76.76", "2001:558:feed::1",
                        "2001:558:feed::2"},
                       {"dot.xfinity.com"} /* dns_over_tls_hostnames */,
                       "https://doh.xfinity.com/dns-query{?dns}",
                       "" /* ui_name */, "" /* privacy_policy */,
                       false /* display_globally */,
                       {} /* display_countries */),
      DohProviderEntry(
          "Dnssb", {"185.222.222.222", "185.184.222.222", "2a09::", "2a09::1"},
          {"dns.sb"} /* dns_over_tls_hostnames */,
          {"https://doh.dns.sb/dns-query?no_ecs=true{&dns}",
           false /* use_post */},
          "DNS.SB" /* ui_name */, "https://dns.sb/privacy" /* privacy_policy */,
          false /* display_globally */, {"DE", "EE"} /* display_countries */),
      DohProviderEntry("Google",
                       {"8.8.8.8", "8.8.4.4", "2001:4860:4860::8888",
                        "2001:4860:4860::8844"},
                       {"dns.google", "dns.google.com",
                        "8888.google"} /* dns_over_tls_hostnames */,
                       "https://dns.google/dns-query{?dns}",
                       "Google" /* ui_name */,
                       "https://developers.google.com/speed/public-dns/"
                       "privacy" /* privacy_policy */,
                       true /* display_globally */, {} /* display_countries */),
      DohProviderEntry("OpenDNS",
                       {"208.67.222.222", "208.67.220.220", "2620:119:35::35",
                        "2620:119:53::53"},
                       {""} /* dns_over_tls_hostnames */,
                       "https://doh.opendns.com/dns-query{?dns}",
                       "OpenDNS" /* ui_name */,
                       "https://www.cisco.com/c/en/us/about/legal/"
                       "privacy-full.html" /* privacy_policy */,
                       true /* display_globally */, {} /* display_countries */),
      DohProviderEntry("OpenDNSFamily",
                       {"208.67.222.123", "208.67.220.123", "2620:119:35::123",
                        "2620:119:53::123"},
                       {""} /* dns_over_tls_hostnames */,
                       "https://doh.familyshield.opendns.com/"
                       "dns-query{?dns}",
                       "" /* ui_name */, "" /* privacy_policy */,
                       false /* display_globally */,
                       {} /* display_countries */),
      DohProviderEntry(
          "Quad9Cdn",
          {"9.9.9.11", "149.112.112.11", "2620:fe::11", "2620:fe::fe:11"},
          {"dns11.quad9.net"} /* dns_over_tls_hostnames */,
          "https://dns11.quad9.net/dns-query", "" /* ui_name */,
          "" /* privacy_policy */, false /* display_globally */,
          {} /* display_countries */),
      DohProviderEntry(
          "Quad9Insecure",
          {"9.9.9.10", "149.112.112.10", "2620:fe::10", "2620:fe::fe:10"},
          {"dns10.quad9.net"} /* dns_over_tls_hostnames */,
          "https://dns10.quad9.net/dns-query", "" /* ui_name */,
          "" /* privacy_policy */, false /* display_globally */,
          {} /* display_countries */),
      DohProviderEntry(
          "Quad9Secure",
          {"9.9.9.9", "149.112.112.112", "2620:fe::fe", "2620:fe::9"},
          {"dns.quad9.net", "dns9.quad9.net"} /* dns_over_tls_hostnames */,
          "https://dns.quad9.net/dns-query", "Quad9" /* ui_name */,
          "https://www.quad9.net/home/privacy/" /* privacy_policy */,
          true /* display_globally */, {} /* display_countries */),
  }};
  return *providers;
}

}  // namespace net
