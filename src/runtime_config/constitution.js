class Action {
  constructor(validate, apply) {
    this.validate = validate;
    this.apply = apply;
  }
}

function parseUrl(url) {
  // From https://tools.ietf.org/html/rfc3986#appendix-B
  const re = new RegExp(
    "^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?"
  );
  const groups = url.match(re);
  if (!groups) {
    throw new TypeError(`${url} is not a valid URL.`);
  }
  return {
    scheme: groups[2],
    authority: groups[4],
    path: groups[5],
    query: groups[7],
    fragment: groups[9],
  };
}

function checkType(value, type, field) {
  const optional = type.endsWith("?");
  if (optional) {
    if (typeof value === null || typeof value === undefined) {
      return;
    }
    type = type.slice(0, -1);
  }
  if (type === "array") {
    if (!Array.isArray(value)) {
      throw new Error(`${field} must be an array`);
    }
  } else if (type === "integer") {
    if (!Number.isInteger(value)) {
      throw new Error(`${field} must be an integer`);
    }
  } else if (typeof value !== type) {
    throw new Error(`${field} must be of type ${type} but is ${typeof value}`);
  }
}

function checkEnum(value, members, field) {
  if (!members.contains(value)) {
    throw new Error(`${field} must be one of ${members}`);
  }
}

function checkBounds(value, low, high, field) {
  if (low !== null && value < low) {
    throw new Error(`${field} must be greater than ${low}`);
  }
  if (high !== null && value > high) {
    throw new Error(`${field} must be lower than ${high}`);
  }
}

function checkLength(value, min, max, field) {
  if (min !== null && value.length < min) {
    throw new Error(`${field} must be an array of minimum ${min} elements`);
  }
  if (max !== null && value.length > max) {
    throw new Error(`${field} must be an array of maximum ${max} elements`);
  }
}

function checkJwks(value, field) {
  checkType(value, "object", field);
  checkType(value.keys, "array", `${field}.keys`);
  for (const [i, jwk] of value.keys.entries()) {
    checkType(jwk.kid, "string", `${field}.keys[${i}].kid`);
    checkType(jwk.kty, "string", `${field}.keys[${i}].kty`);
    checkType(jwk.x5c, "array", `${field}.keys[${i}].x5c`);
    checkLength(jwk.x5c, 1, null, `${field}.keys[${i}].x5c`);
    for (const [j, b64der] of jwk.x5c.entries()) {
      checkType(b64der, "string", `${field}.keys[${i}].x5c[${j}]`);
      const pem =
        "-----BEGIN CERTIFICATE-----" + b64der + "-----END CERTIFICATE-----";
      if (!ccf.isValidX509Chain(pem)) {
        throw new Error(
          `${field}.keys[${i}].x5c[${j}] is not an X509 certificate`
        );
      }
    }
  }
}

const actions = new Map([
  [
    "set_member_data",
    new Action(
      function (args) {
        // Check that member id is a valid entity id?
        checkType(args.member_id, "string", "member_id");
        checkType(args.member_data, "object", "member_data");
      },

      function (args) {
        let member_id = ccf.strToBuf(args.member_id);
        let members_info = ccf.kv["public:ccf.gov.members.info"];
        let member_info = members_info.get(member_id);
        if (member_info === undefined) {
          throw new Error(`Member ${args.member_id} does not exist`);
        }
        let mi = ccf.bufToJsonCompatible(member_info);
        mi.member_data = args.member_data;
        members_info.set(member_id, ccf.jsonCompatibleToBuf(mi));
      }
    ),
  ],
  [
    "rekey_ledger",
    new Action(
      function (args) {},

      function (args) {
        ccf.node.rekeyLedger();
      }
    ),
  ],
  [
    "transition_service_to_open",
    new Action(
      function (args) {
        // Check that args is null?
      },

      function (args) {
        ccf.node.transitionServiceToOpen();
      }
    ),
  ],
  [
    "set_user",
    new Action(
      function (args) {
        // Check that args is null?
      },

      function (args) {
        let user_id = ccf.pemToId(args.cert);
        let raw_user_id = ccf.strToBuf(user_id);

        if (ccf.kv["ccf.gov.users.certs"].has(raw_user_id)) {
          console.log(`User cert for ${user_id} already exists`);
          return; // Idempotent
        }

        ccf.kv["ccf.gov.users.certs"].set(raw_user_id, ccf.strToBuf(args.cert));

        if (args.user_data != null) {
          if (ccf.kv["ccf.gov.users.info"].has(raw_user_id)) {
            throw new Error(`User info for ${user_id} already exists`);
            // Internal error
          }

          ccf.kv["ccf.gov.users.info"].set(
            raw_user_id,
            ccf.jsonCompatibleToBuf(args.user_data)
          );
        }
      }
    ),
  ],
  [
    "set_recovery_threshold",
    new Action(
      function (args) {
        checkType(args.threshold, "integer", "threshold");
        checkBounds(args.threshold, 1, 254, "threshold");
      },
      function (args) {}
    ),
  ],
  [
    "always_accept_noop",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_reject_noop",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_accept_with_one_vote",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_reject_with_one_vote",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_accept_if_voted_by_operator",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_accept_if_proposed_by_operator",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_accept_with_two_votes",
    new Action(
      function (args) {},
      function (args) {}
    ),
  ],
  [
    "always_reject_with_two_votes",
    new Action(
      function (args) {},
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
          checkType(
            args.key_policy.sgx_claims,
            "object?",
            "key_policy.sgx_claims"
          );
          if (args.key_policy.sgx_claims) {
            for (const [name, value] of Object.entries(
              args.key_policy.sgx_claims
            )) {
              checkType(value, "string", `key_policy.sgx_claims["${name}"]`);
            }
          }
        }
        checkType(args.jwks, "object?", "jwks");
        if (args.jwks) {
          checkJwks(args.jwks, "jwks");
        }
        if (args.auto_refresh) {
          if (!args.ca_cert_bundle_name) {
            throw new Error(
              "ca_cert_bundle_name is missing but required if auto_refresh is true"
            );
          }
          let url;
          try {
            url = parseUrl(args.issuer);
          } catch (e) {
            throw new Error("issuer must be a URL if auto_refresh is true");
          }
          if (url.scheme != "https") {
            throw new Error(
              "issuer must be a URL starting with https:// if auto_refresh is true"
            );
          }
          if (url.query || url.fragment) {
            throw new Error(
              "issuer must be a URL without query/fragment if auto_refresh is true"
            );
          }
        }
      },
      function (args) {
        if (args.auto_refresh) {
          const caCertBundleName = args.ca_cert_bundle_name;
          const caCertBundleNameBuf = ccf.strToBuf(args.ca_cert_bundle_name);
          if (
            !ccf.kv["public:ccf.gov.tls.ca_cert_bundles"].has(
              caCertBundleNameBuf
            )
          ) {
            throw new Error(
              `No CA cert bundle found with name '${caCertBundleName}'`
            );
          }
        }
        const issuer = args.issuer;
        const jwks = args.jwks;
        delete args.jwks;
        const metadata = args;
        if (jwks) {
          ccf.setJwtPublicSigningKeys(issuer, metadata, jwks);
        }
        const issuerBuf = ccf.strToBuf(issuer);
        const metadataBuf = ccf.jsonCompatibleToBuf(metadata);
        ccf.kv["public:ccf.gov.jwt.issuers"].set(issuerBuf, metadataBuf);
      }
    ),
  ],
  [
    "set_jwt_public_signing_keys",
    new Action(
      function (args) {
        checkType(args.issuer, "string", "issuer");
        checkJwks(args.jwks, "jwks");
      },
      function (args) {
        const issuer = args.issuer;
        const issuerBuf = ccf.strToBuf(issuer);
        const metadataBuf = ccf.kv["public:ccf.gov.jwt.issuers"].get(issuerBuf);
        if (metadataBuf === undefined) {
          throw new Error(`issuer ${issuer} not found`);
        }
        const metadata = ccf.bufToJsonCompatible(metadataBuf);
        const jwks = args.jwks;
        ccf.setJwtPublicSigningKeys(issuer, metadata, jwks);
      }
    ),
  ],
  [
    "remove_jwt_issuer",
    new Action(
      function (args) {
        checkType(args.issuer, "string", "issuer");
      },
      function (args) {
        const issuerBuf = ccf.strToBuf(args.issuer);
        if (!ccf.kv["public:ccf.gov.jwt.issuers"].delete(issuerBuf)) {
          return;
        }
        ccf.removeJwtPublicSigningKeys(args.issuer);
      }
    ),
  ],
]);

export function validate(input) {
  let proposal = JSON.parse(input);
  let errors = [];
  let position = 0;
  for (const action of proposal["actions"]) {
    const definition = actions.get(action.name);
    if (definition) {
      try {
        definition.validate(action.args);
      } catch (e) {
        errors.push(
          `${action.name} at position ${position} failed validation: ${e}`
        );
      }
    } else {
      errors.push(`${action.name}: no such action`);
    }
    position++;
  }
  return { valid: errors.length === 0, description: errors.join(", ") };
}

export function resolve(proposal, proposer_id, votes) {
  const actions = JSON.parse(proposal)["actions"];
  if (actions.length === 1) {
    if (actions[0].name === "always_accept_noop") {
      return "Accepted";
    } else if (actions[0].name === "always_reject_noop") {
      return "Rejected";
    } else if (
      actions[0].name === "always_accept_with_one_vote" &&
      votes.length === 1 &&
      votes[0].vote === true
    ) {
      return "Accepted";
    } else if (
      actions[0].name === "always_reject_with_one_vote" &&
      votes.length === 1 &&
      votes[0].vote === false
    ) {
      return "Rejected";
    } else if (actions[0].name === "always_accept_if_voted_by_operator") {
      for (const vote of votes) {
        const mi = ccf.kv["public:ccf.gov.members.info"].get(
          ccf.strToBuf(vote.member_id)
        );
        if (mi && ccf.bufToJsonCompatible(mi).member_data.is_operator) {
          return "Accepted";
        }
      }
    } else if (
      actions[0].name === "always_accept_if_proposed_by_operator" ||
      actions[0].name === "remove_user"
    ) {
      const mi = ccf.kv["public:ccf.gov.members.info"].get(
        ccf.strToBuf(proposer_id)
      );
      if (mi && ccf.bufToJsonCompatible(mi).member_data.is_operator) {
        return "Accepted";
      }
    } else if (
      actions[0].name === "always_accept_with_two_votes" &&
      votes.length === 2 &&
      votes[0].vote === true &&
      votes[1].vote === true
    ) {
      return "Accepted";
    } else if (
      actions[0].name === "always_reject_with_two_votes" &&
      votes.length === 2 &&
      votes[0].vote === false &&
      votes[1].vote === false
    ) {
      return "Rejected";
    } else {
      // For all other proposals (i.e. the real ones), use member
      // majority
      const memberVoteCount = votes.filter((v) => v.vote).length;

      let activeMemberCount = 0;
      ccf.kv["public:ccf.gov.members.info"].forEach((v) => {
        const info = ccf.bufToJsonCompatible(v);
        if (info.status === "Active") {
          activeMemberCount++;
        }
      });

      // A majority of members can accept a proposal.
      if (memberVoteCount > Math.floor(activeMemberCount / 2)) {
        return "Accepted";
      }

      return "Open";
    }
  }

  return "Open";
}

export function apply(proposal) {
  const proposed_actions = JSON.parse(proposal)["actions"];
  for (const proposed_action of proposed_actions) {
    const definition = actions.get(proposed_action.name);
    definition.apply(proposed_action.args);
  }
}
