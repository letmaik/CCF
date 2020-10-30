// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include <msgpack/msgpack.hpp>
#include "ds/json.h"
#include "entities.h"
#include "kv/map.h"

namespace ccf
{
  struct JwtIssuerKeyPolicy
  {
    // OE claim name -> hex-encoded claim value
    // See openenclave/attestation/verifier.h
    std::map<std::string, std::string> sgx_claims;

    MSGPACK_DEFINE(
      sgx_claims);
  };

  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(JwtIssuerKeyPolicy);
  // Current limitation of the JSON macros: It is necessary to defined
  // DECLARE_JSON_REQUIRED_FIELDS even though there are no required
  // fields. This raises some compiler warnings that are disabled locally.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
  DECLARE_JSON_REQUIRED_FIELDS(JwtIssuerKeyPolicy);
#pragma clang diagnostic pop
  DECLARE_JSON_OPTIONAL_FIELDS(JwtIssuerKeyPolicy, sgx_claims)

  enum class JwtIssuerKeyFilter
  {
    All,
    SGX
  };

  DECLARE_JSON_ENUM(
    JwtIssuerKeyFilter,
    {{JwtIssuerKeyFilter::All, "all"},
     {JwtIssuerKeyFilter::SGX, "sgx"}});

  struct JwtIssuerMetadata
  {
    bool validate_issuer;
    JwtIssuerKeyFilter key_filter;
    JwtIssuerKeyPolicy key_policy;

    MSGPACK_DEFINE(
      validate_issuer,
      key_filter,
      key_policy);
  };

  DECLARE_JSON_TYPE(JwtIssuerMetadata);
  DECLARE_JSON_REQUIRED_FIELDS(
    JwtIssuerMetadata,
    validate_issuer,
    key_filter,
    key_policy);

  using JwtIssuer = std::string;
  using JwtKeyId = std::string;

  using JwtIssuers = kv::Map<JwtIssuer, JwtIssuerMetadata>;
  using JwtIssuerKeyIds = kv::Map<JwtIssuer, std::vector<JwtKeyId>>;
  using JwtPublicSigningKeys = kv::Map<JwtKeyId, Cert>;
  using JwtPublicSigningKeysValidateIssuer = kv::Map<JwtKeyId, bool>;
}

MSGPACK_ADD_ENUM(ccf::JwtIssuerKeyFilter);
