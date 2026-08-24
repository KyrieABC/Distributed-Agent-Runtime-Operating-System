#pragma once

//c++ Macro: substitue text or code before actual compilation process begins

#include "dar/common/status.h"

// recoverable runtime errors propagate through Status
//-> invalid input, missing object, network unavailabl, deadline exceeded, resource exhausted (all considered as Status objects and passed leftward)

//programmer bug should be caught with invaraints/assertion/tests


// use ::dar(namespace)::Status(class)

// #define: C++ preprocessor to define a macro

// expr -> accept one argument for the macro named "DAR_RETURN_IF_ERROR"
// Ex: DAR_RETURN_IF_ERROR(ConnectToNode()), replace expr to ConnectToNode()

// \ continue macro definition onto next physical line

// Typical macro only executed once (do-while loop for safety)??

// ::dar::Status evaluate expr(returned by expr) and stores the resulting status
// ::(beginning)->lookup from global namespace(top-level DAR namespace)

// If not okay(!ok()) (evaluate result is not kOk)
// If okay, doesn't return anything


#define DAR_RETURN_IF_ERROR(expr)                     \
    do{                                                \
        const ::dar::Status _dar_status = (expr);      \
        if(!_dar_status.ok())                          \
        {                                              \
            return _dar_status;                        \
        }                                               \
    }while(false)                                 