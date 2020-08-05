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

Preparing an app for deployment requires a build step that
- transforms bare imports (`lodash`) into relative imports (`./node_modules/lodash/lodash.js`),
- transforms old-style CommonJS modules into native JavaScript modules.

For this, we rely on [rollup](https://rollupjs.org), which also offers tree shaking support
to avoid deploying unused modules. See `package.json` and `rollup.config.js` for details.

After running `npm run build`, the `dist/` folder contains:
- `node_modules/` -> external packages
- `src/endpoints.js` -> application code

The Python client of CCF can now be used to deploy the application.

TODO extend Python client

TODO Decide on how to handle app namespaces during deployment.
     It should be easy to clone a generic app from somewhere and deploy it under a given
     REST API and module path prefix (potentially, these two are separate).
     This should probably be stored in some deployment config file, like in k8s.
     No CLI args, if possible.

### A note on CommonJS modules

The sample project uses the [@rollup/plugin-commonjs](https://github.com/rollup/plugins/tree/master/packages/commonjs) package to automatically convert CommonJS modules to native JavaScript modules
so that they can be used in CCF.

Sometimes this conversion can fail, for example when the package has circular module dependencies.
If that is the case, try one of the following suggestions:

1. Check if there is a JavaScript module variant of the package and use that instead.
   These are also named ES or ECMAScript modules/packages.

2. Check if there is a known work-around to fix the conversion issue.
   Chances are you are not the only one experiencing it.

3. Manually wrap a browser bundle of the package without using npm.

An example where step 3 is currently necessary is [protobuf.js](https://github.com/protobufjs/protobuf.js/issues/1402). To wrap the bundle as a module, save the following code as `protobuf.js` in your project:

```js
let exports = {}, module = {exports};

// REPLACE this comment with the content of
// https://raw.githubusercontent.com/protobufjs/protobuf.js/v6.10.1/dist/protobuf.min.js

export default module.exports;
```

You are now able to import protobuf.js:
```js
import pb from './protobuf.js';
```
