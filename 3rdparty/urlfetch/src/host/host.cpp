// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <iostream>

#include <openenclave/host.h>

#include "urlfetch_u.h"

int main(int argc, const char* argv[])
{
    oe_result_t result;
    int ret = 1;
    oe_enclave_t* enclave = NULL;

    uint32_t flags = OE_ENCLAVE_FLAG_DEBUG;

    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0] << " url callback_url nonce" << std::endl;
        goto exit;
    }

    // Create the enclave
    result = oe_create_urlfetch_enclave(
        argv[1], OE_ENCLAVE_TYPE_AUTO, flags, NULL, 0, &enclave);
    if (result != OE_OK)
    {
        std::cerr << "oe_create_urlfetch_enclave(): result=" << result 
                  << " (" << oe_result_str(result) << ")" << std::endl;
        goto exit;
    }

    // Call into the enclave
    result = enclave_main(enclave);
    if (result != OE_OK)
    {
        std::cerr << "calling into enclave_main failed: result=" << result 
                  << " (" << oe_result_str(result) << ")" << std::endl;
        goto exit;
    }
    
    // Send results to callback URL


    ret = 0;

exit:
    // Clean up the enclave if we created one
    if (enclave)
        oe_terminate_enclave(enclave);

    return ret;
}