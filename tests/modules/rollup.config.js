import { nodeResolve } from '@rollup/plugin-node-resolve';

export default {
  input: 'src/endpoints.js',
  output: {
    dir: 'dist',
    format: 'es',
    preserveModules: true
  },
  plugins: [nodeResolve({
    modulesOnly: true
  })]
};