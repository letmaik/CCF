// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/json.h"
#include "http/http_status.h"

namespace ccf
{
  struct ODataError
  {
    std::string code;
    std::string message;
  };

  DECLARE_JSON_TYPE(ODataError);
  DECLARE_JSON_REQUIRED_FIELDS(ODataError, code, message);

  struct ODataErrorResponse
  {
    ODataError error;
  };

  DECLARE_JSON_TYPE(ODataErrorResponse);
  DECLARE_JSON_REQUIRED_FIELDS(ODataErrorResponse, error);

  struct ErrorDetails
  {
    http_status status;
    std::string code;
    std::string msg;
  };

  namespace errors
  {
#define ERROR(code) constexpr const char* code = #code;

    // See
    // https://docs.microsoft.com/en-us/rest/api/storageservices/common-rest-api-error-codes
    // for inspiration.

    // Generic errors
    ERROR(AuthorizationFailed)
    ERROR(InternalError)
    ERROR(InvalidAuthenticationInfo)
    ERROR(InvalidHeaderValue)
    ERROR(InvalidInput)
    ERROR(InvalidResourceName)
    ERROR(MissingRequiredHeader)
    ERROR(ResourceNotFound)
    ERROR(RequestNotSigned)
    ERROR(UnsupportedHttpVerb)

    // CCF-specific errors
    ERROR(ConsensusTypeMismatch)
    ERROR(FrontendNotOpen)
    ERROR(InvalidQuote)
    ERROR(InvalidNodeState)
    ERROR(KeyNotFound)
    ERROR(NodeAlreadyExists)
    ERROR(ProposalNotOpen)
    ERROR(ProposalNotFound)
    ERROR(StateDigestMismatch)
    ERROR(TransactionNotFound)
    ERROR(TransactionCommitAttemptsExceedLimit)
    ERROR(UnknownCertificate)
    ERROR(VoteNotFound)
    ERROR(VoteAlreadyExists)

#undef ERROR
  }
}