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
- write JavaScript-based endpoints and constitutions,
- share code between endpoints using modules, and
- import existing non-module libraries. 

## Import existing non-module libraries

Assumptions:
- Library is offered as a single script file.
- Script file has CommonJS (`exports`) or Node.js exports (`module.exports`).
- Library has no external dependencies to other libraries.

An example is the [Lodash](https://lodash.com/) library.

To make Lodash available for import to JavaScript modules we need to export it first.
We can do that by wrapping the published bundle in a module.

Save the following module code as `lodash.js`:
```js
let exports = {}, module = {exports};

// REPLACE this comment with the content of
// https://raw.githubusercontent.com/lodash/lodash/4.17.15-npm/core.js

export default let _ = exports._
```

You are now able to import Lodash from another module:
```js
import _ from 'lodash.js'

export default function(arr) {
    return _.partition(arr, n => n % 2);
}
```

## Import existing non-module libraries with external dependencies

Assumptions:
- Library is offered as a single script file.
- Script file has CommonJS (`exports`) or Node.js exports (`module.exports`).
- Library uses external libraries that must be in global scope.

TODO Is this too advanced? Libraries usually bundle dependencies if they are small.
TODO only go ahead if there's an example that somehow fits CCF
