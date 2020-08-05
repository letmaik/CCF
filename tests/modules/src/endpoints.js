import {partition} from 'lodash-es'
import * as rs  from 'jsrsasign';
import * as pbjs from './libs/protobuf.js'

export function put() {
    console.log(partition)
}

export function get() {
    console.log(pbjs)
    console.log(rs)
}