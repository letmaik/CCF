# JavaScript Modules

## Introduction

CCF supports JavaScript exclusively through native JavaScript modules, stored in the KV store.

Typical web applications running in browsers have a top-level module that does not
export functions but rather modifies the global browser state.
In CCF, JavaScript-based endpoints are top-level modules which instead export a single
function that is called by CCF from C++.

Most web applications and libraries do not yet make use of native JavaScript modules when they are deployed.
This has various reasons related to performance, browser support, and others.
Instead, a pre-deployment step called "bundling" converts and concatenates all JavaScript modules
into a single script that is not a native JavaScript module anymore.
In CCF, loading such bundles is not directly supported as everything has to be a module.

This guide describes how to
- write JavaScript endpoints and constitutions,
- share code using modules, and
- using existing libraries.

## JavaScript endpoints and constitutions

TBD

## Sharing code using modules

TBD

## Using existing library bundles

A library bundle is a library that has been bundled in a single script file,
typically for use in web browsers.

In the following we only consider libraries that do not depend on [Node.js APIs](https://nodejs.org/api/) or [Web APIs](https://developer.mozilla.org/en-US/docs/Web/API).
Such APIs are not available in CCF's JavaScript runtime.

A library bundle supports one or more runtime environments depending on how its members are exported.
In the following we describe how to use bundles that support the CommonJS, Node.js, and global exports.
If a bundle supports all export variants, either method will work.

### Library bundle with CommonJS/Node.js exports

We assume that the bundle uses CommonJS (`exports`) or Node.js exports (`module.exports`).

An example is the [Lodash](https://lodash.com/) library.

To make Lodash available for import in JavaScript modules we need to export it first.
We can do that by wrapping the published bundle in a module.

Save the following module code as `lodash.js`:
```js
let exports = {}, module = {exports};

// REPLACE this comment with the content of
// https://raw.githubusercontent.com/lodash/lodash/4.17.15-npm/core.js

export default module.exports;
```

You are now able to import Lodash from another module:
```js
import _ from './lodash.js';

export function partition(arr) {
    return _.partition(arr, n => n % 2);
}
```

### Library bundle with global exports

We assume that the bundle defines variables in the global scope.

An example is the browser bundle of the [jsrsasign](https://github.com/kjur/jsrsasign) library.

To make jsrsasign available for import in JavaScript modules we need to export it first.
We can do that by wrapping the published bundle in a module.

Save the following module code as `jsrsasign.js`:
```js
// Pretend we are a browser.
let navigator = {}, window = {};

// REPLACE this comment with the content of
// https://cdnjs.cloudflare.com/ajax/libs/jsrsasign/8.0.20/jsrsasign-all-min.min.js

export {KJUR, KEYUTIL, X509, RSAKey};
```

You are now able to import jsrsasign from another module:
```js
import * as rs from './jsrasign.mjs';

export function sign(pem, pwd, content) {
    let prvKey = rs.KEYUTIL.getKey(pem, pwd);
    let sig = new rs.KJUR.crypto.Signature({alg: 'SHA1withRSA'});
    sig.init(prvKey);
    sig.updateString(content);
    let sigVal = sig.sign();
    return sigVal
}
```

### Dependencies

Library A might have a dependency on another external library B.
In this case, the dependency has to be imported into the `a.js` module for A:

```js
import * as B from './b.js';

// ...
```

TODO concrete example

## Using existing module libraries

### Module libraries without dependencies

Some libraries like [Lodash](https://lodash.com/) are already available as collections of [native JavaScript modules](https://github.com/lodash/lodash/tree/es).
These can be stored in CCF as individual modules under a prefix, e.g. `lodash/`.
Once stored, they can be imported like `import {partition} from 'lodash/partition.js`.
Different to Node.js the full path is required to import the main module: `import _ from 'lodash/lodash.js`.

### Module libraries with dependencies

Currently, CCF does not easily support using module libraries that have external dependencies.
The following explains why that is and a proposal that may be implemented in the future.

There is no generally agreed on method for how dependency imports are resolved across JS engines.

Node.js uses bare identifiers corresponding to npm packages, e.g. `import _ from 'lodash'`.
All packages are installed under a `node_modules` subfolder which Node.js inspects at runtime.

Web browsers currently require URLs, e.g. `import _ from 'https://unpkg.com/lodash-es'`.
Tooling exists that rewrites URLs to other URLs or relative paths before deployment.
This avoids relying on external servers.

There is a proposal for [Import Maps](https://github.com/WICG/import-maps) that aims to
bring some of Node.js' concepts natively to the web.
Instead of requiring URLs when referring to dependencies, bare identifiers can then be used.
These are resolved using an Import Map which can resolve them to URLs or relative paths.

If and when Import Maps are more widely supported and developer tooling becomes available
it may make sense to support them in CCF as well to more easily consume module libraries.

## CCF apps

### NPM

- All used npm dependencies must be of JS module type
- Legacy libraries must be manually wrapped as explained above

A CCF app is a combination of:
- App metadata including endpoint description (JSON)
- Endpoint module with one exported function per endpoint
- Supporting modules, either external dependencies or app logic

Preparing an app for deployment requires a build step that transforms bare imports
into relative imports. For this, we can use rollup, which also provides tree shaking
to avoid deploying unused modules. See `package.json` and `rollup.config.js` for details.

An app is deployed to CCF in multiple tables:
- `ccf.modules`: App JS modules stored under a prefix, e.g. `/app1/`.
- `ccf.endpoints` (?): Endpoint metadata.

#### Legacy non-module packages

While most modern packages have transitioned to JS modules, there are still packages that
are published as traditional CommonJS/Node.js-export packages.
The easiest way to consume such packages is to find a bundled variant of them
and wrap them in a module as described earlier.

Supporting traditional packages directly by emulating `require()` in CCF is a complex task
and not planned.
