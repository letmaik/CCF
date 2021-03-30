class Action {
  constructor(validate, apply) {
    this.validate = validate;
    this.apply = apply;
  }
}

function parseUrl(url) {
  // From https://tools.ietf.org/html/rfc3986#appendix-B
  const re = new RegExp("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?");
  const groups = url.match(re);
  if (!groups) {
    throw new TypeError(`${url} is not a valid URL.`);
  }
  return {
    scheme: groups[2],
    authority: groups[4],
    path: groups[5],
    query: groups[7],
    fragment: groups[9]
  }
}

function checkType(value, type, field) {
  const optional = type.endsWith('?');
  if (optional) {
    if (typeof value === null || typeof value === undefined) {
      return;
    }
    type = type.slice(0, -1);
  }
  if ((type === "array" && !Array.isArray(value)) ||
      (type === "integer" && !Number.isInteger(value)) ||
      typeof value !== type
      ) {
    throw new Error(`${field} must be of type ${type} but is ${typeof value}`);
  }
}

function checkEnum(value, members, field) {
  if (!members.contains(value)) {
    throw new Error(`${field} must be one of ${members}`);
  }
}

function checkBounds(value, low, high, field) {
  if (value < low || value > high) {
    throw new Error(`${field} must be within ${low} and ${high}`);
  }
}

const actions = new Map([
  [
    "set_recovery_threshold",
    new Action(
      function (args) {
        checkType(args.threshold, 'integer', 'threshold');
        checkBounds(args.threshold, 1, 254, 'threshold');
      },
      function (args) {}
    ),
  ],
  [
    "always_accept_noop",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_reject_noop",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_accept_with_one_vote",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_reject_with_one_vote",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_accept_if_voted_by_operator",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_accept_if_proposed_by_operator",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_accept_with_two_votes",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "always_reject_with_two_votes",
    new Action(
      function (args) {
      },
      function (args) {}
    ),
  ],
  [
    "remove_user",
    new Action(
      function (args) {
        checkType(args.user_id, "string", "user_id");
      },
      function (args) {
        const user_id = ccf.strToBuf(args.user_id);
        ccf.kv["public:ccf.gov.users.certs"].delete(user_id);
        ccf.kv["public:ccf.gov.users.info"].delete(user_id);
      }
    ),
  ],
  [
    "set_jwt_issuer",
    new Action(
      function (args) {
        checkType(args.issuer, "string", "issuer");
        checkType(args.auto_refresh, "boolean?", "auto_refresh");
        checkType(args.ca_cert_bundle_name, "string?", "ca_cert_bundle_name");
        checkEnum(args.key_filter, ["all", "sgx"], "key_filter");
        checkType(args.key_policy, "object?", "key_policy");
        if (args.key_policy) {
          checkType(args.key_policy, "object?", "key_policy.sgx_claims");
          if (args.key_policy.sgx_claims) {
            for (const [name, value] of Object.entries(args.key_policy)) {
              checkType(value, "string", `key_policy["${name}"]`);
            }
          }
        }
        checkType(args.jwks, "object?", "jwks");
        if (args.jwks) {
          checkType(args.jwks.keys, "array", "jwks.keys");
          for (const jwk of args.jwks.keys) {
            checkType(jwk.kid, "string", "jwks.keys[].kid");
            checkType(jwk.kty, "string", "jwks.keys[].kty");
            checkType(jwk.x5c, "array", "jwks.keys[].x5c");
            for (const b64der of jwk.x5c) {
              checkType(b64der, "string", "jwks.keys[].x5c[]");
              const pem = "-----BEGIN CERTIFICATE-----" + b64der + "-----END CERTIFICATE-----";
              if (!ccf.isValidX509Cert(pem)) {
                throw new Error(`jwks.keys[].x5c[] is not an X509 certificate`);
              }
            }
          }
        }
        if (args.auto_refresh) {
          if (!args.ca_cert_bundle_name) {
            throw new Error("ca_cert_bundle_name is missing but required if auto_refresh is true");
          }
          let url
          try {
            url = parseUrl(args.issuer);
          } catch (e) {
            throw new Error("issuer must be a URL if auto_refresh is true");
          }
          if (url.scheme != "https") {
            throw new Error("issuer must be a URL starting with https:// if auto_refresh is true");
          }
          if (url.query || url.fragment) {
            throw new Error("issuer must be a URL without query/fragment if auto_refresh is true");
          }
        }
      },
      function (args) {
        if (args.auto_refresh) {
          const ca_cert_bundle_name = ccf.strToBuf(args.ca_cert_bundle_name);
          if (!ccf.kv["public:ccf.gov.tls.ca_cert_bundles"].has(ca_cert_bundle_name)) {
            throw new Error(`No CA cert bundle found with name '${args.ca_cert_bundle_name}'`);
          }
        }
        if (!ccf.setJwtPublicSigningKeys(args)) {
          throw new Error("setJwtPublicSigningKeys() failed");
        }

        const issuer = ccf.strToBuf(args.issuer);
        delete args.jwks;
        const metadata = ccf.jsonCompatibleToBuf(args);
        ccf.kv["public:ccf.gov.jwt.issuers"].set(issuer, metadata);
      }
    ),
  ],
]);

function validate(input) {
  let proposal = JSON.parse(input);
  let errors = [];
  let position = 0;
  for (const action of proposal["actions"]) {
    const definition = actions.get(action.name);
    if (definition) {
      try {
        definition.validate(action.args);
      } catch (e) {
        errors.push(`${action.name} at position ${position} failed validation: ${e}`);
      }
    } else {
      errors.push(`${action.name}: no such action`);
    }
    position++;
  }
  return { valid: errors.length === 0, description: errors.join(", ") };
}

function resolve(proposal, proposer_id, votes) {
  const actions = JSON.parse(proposal)["actions"];
  if (actions.length === 1) {
    if (actions[0].name === "always_accept_noop") {
      return "Accepted";
    }
    if (actions[0].name === "always_reject_noop") {
      return "Rejected";
    }
    if (
      actions[0].name === "always_accept_with_one_vote" &&
      votes.length === 1 &&
      votes[0].vote === true
    ) {
      return "Accepted";
    }
    if (
      actions[0].name === "always_reject_with_one_vote" &&
      votes.length === 1 &&
      votes[0].vote === false
    ) {
      return "Rejected";
    }
    if (actions[0].name === "always_accept_if_voted_by_operator") {
      for (const vote of votes) {
        const mi = ccf.kv["public:ccf.gov.members.info"].get(
          ccf.strToBuf(vote.member_id)
        );
        if (mi && ccf.bufToJsonCompatible(mi).member_data.is_operator) {
          return "Accepted";
        }
      }
    }
    if (
      actions[0].name === "always_accept_if_proposed_by_operator" ||
      actions[0].name === "remove_user"
    ) {
      const mi = ccf.kv["public:ccf.gov.members.info"].get(
        ccf.strToBuf(proposer_id)
      );
      if (mi && ccf.bufToJsonCompatible(mi).member_data.is_operator) {
        return "Accepted";
      }
    }
    if (
      actions[0].name === "always_accept_with_two_votes" &&
      votes.length === 2 &&
      votes[0].vote === true &&
      votes[1].vote === true
    ) {
      return "Accepted";
    }
    if (
      actions[0].name === "always_reject_with_two_votes" &&
      votes.length === 2 &&
      votes[0].vote === false &&
      votes[1].vote === false
    ) {
      return "Rejected";
    }
  }

  return "Open";
}

function apply(proposal) {
  const proposed_actions = JSON.parse(proposal)["actions"];
  for (const proposed_action of proposed_actions) {
    const definition = actions.get(proposed_action.name);
    definition.apply(proposed_action.args);
  }
}
