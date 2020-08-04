# JavaScript in CCF

## Introduction

CCF supports JavaScript for the constitution, proposals, and applications.

The JavaScript environment is similar to that in browsers, with the following differences:
- only [JavaScript modules](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Modules) are supported,
- no [Web APIs](https://developer.mozilla.org/en-US/docs/Web/API) are available,
- module imports cannot be URLs and must be relative paths.

The constitution and proposal scripts are considered special modules that cannot import other modules.
This may change in the future though.

An application is a combination of one or more modules and a REST endpoint description.
Application modules are stored separately from constitution and proposal modules.
Each application module is stored with its full module path, for example, `/app1/endpoint.js`.

Typical web applications running in browsers have a top-level/entry-point module that does not
export functions but rather modifies the global browser state.
In CCF, entry-point modules, like the constitution or application endpoints, instead export
one or more functions that are called by CCF from C++.

This guide describes how to write JavaScript constitutions, proposals, and applications.

## Constitution

TBD

## Proposals

TBD

## Applications

A standard way to build web applications is to rely on Node.js and the npm package manager.
CCF applications can be built the same way with some restrictions:

- All npm packages used in the application must use native JavaScript modules.
  Those using different module formats must be manually wrapped (see later section).
- Application code cannot rely on [Web APIs](https://developer.mozilla.org/en-US/docs/Web/API) or
  [Node.js APIs](https://nodejs.org/api/) being available.
  CCF may add support for a subset of Web APIs in the future.

A CCF app is a combination of
- REST endpoint metadata (JSON),
- an entry-point module with one exported function per endpoint, and
- optionally, a set of extra modules, either self-written or from npm packages.

The recommended project layout is as follows:
```
ccf-app/
  src/
    endpoints.js (entry-point module)
    ...
  ccf.endpoints.json (REST endpoint metadata)
  package.json
  package-lock.json (auto-generated)
  rollup.config.js
```

Take a look at the sample project in xzy/. TODO

Before continuing, make sure you have a recent version of Node.js and npm installed on your system.

Preparing an app for deployment requires a build step that transforms bare imports (`lodash`)
into relative imports (`./node_modules/lodash/lodash.js`).
For this, we rely on [rollup](https://rollupjs.org), which also offers tree shaking support
to avoid deploying unused modules. See `package.json` and `rollup.config.js` for details.

After running `npm run build`, the `dist/` folder contains:
- `node_modules/lodash-es/*.js`
- `src/endpoints.js`

The Python client of CCF can now be used to deploy the application.

TODO extend Python client
TODO decide on how to handle app namespaces during deployment
     it should be easy to clone a generic app from somewhere and deploy it under a given
     REST API and module path prefix (potentially, these two are separate)
     this should probably be stored in some deployment config file, like in k8s
     no CLI args, if possible

### Using npm packages

The sample application from the previous section uses the `lodash-es` npm package
which contains a set of native JavaScript modules.
Any package that is offered as JavaScript modules, also called ES modules or ECMAScript modules,
can be used as-is and added to the application by running `npm install --save lodash-es`.
Note that the build command of the sample application automatically rejects any packages
that are not in the form of JavaScript modules.

Some packages use different module formats, typically the CommonJS/Node.js format.
Supporting such module formats directly in CCF, e.g. by emulating `require()`,
is a complex task and currently not planned.
It is expected that most popular packages will transition to native JavaScript modules in the near future.
In the meantime, the sections below provide guidance on how to easily wrap such packages
as JavaScript modules for use in CCF's JavaScript environment.

### Packages without JavaScript modules

While most modern packages have transitioned to JavaScript modules, there are still packages that
are published as traditional CommonJS/Node.js packages.
The easiest way to consume such packages is to find a bundled variant of them
and wrap them in a JavaScript module.

A bundle is a package that has been bundled in a single script file, typically for use in web browsers.

A bundle supports one or more runtime environments depending on how its members are exported.
In the following we describe how to use bundles that support the CommonJS, Node.js, and global exports.
If a bundle supports all export variants, either method will work.

#### Wrapping bundles with CommonJS/Node.js exports

We assume that the bundle uses CommonJS (`exports`) or Node.js exports (`module.exports`).

An example is the [Lodash](https://lodash.com/) library.
(Note that Lodash is also published as JavaScript module variant.)

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

#### Wrapping bundles with global exports

We assume that the bundle defines variables in the global scope.

An example is the browser bundle of the [jsrsasign](https://github.com/kjur/jsrsasign) package.

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

#### Dependencies between bundles

Package A might have a dependency on another package B not part of the bundle of A.
In this case, the dependency has to be imported into the `a.js` module for A:

```js
import * as B from './b.js';

// ...
```
