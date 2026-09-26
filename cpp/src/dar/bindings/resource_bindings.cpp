#include "dar/bindings/binding_types.h"

#include <string>
#include <utility>

#include <pybind11/pybind11.h>

#include "dar/core/resource.h"

namespace dar::bindings
{
    namespace py = pybind11;

    // Python dict -> ResourceRequest
    /**
     * Convert Python resource syntax
     *   { "CPU": 2.0, "GPU": 0.5}
     * into DAR's native:
     *   ResourceRequest -> ResourceSet
     */
    // The function performs Language Boundary conversion only
    /**
     * ResourceSet::Set() remains authoritative for:
     *   - valid resource name
     *   - finite quantities
     *   - non-negative quantities
     *   - fixed-point conversion
     *   - zero-resource removal
     */
    ResourceRequest ResourceRequestFromDict(const py::dict& resources)
    {
        ResourceSet resource_set;

        for(const auto& item: resources)
        {
            // Resource name
            // Python: {"CPU": 2.0}
            // -> Python facing resource format requires dictionary keys to be strings
            // check python type due to Language/API requirement
            if(!py::isinstance<py::str>(item.first))
            {
                throw py::type_error("resource names must be strings");
            }

            const std::string name = py::cast<std::string>(item.first);

            // Resource Quantity
            /**
             * pybind11 perform python -> double conversion
             * 
             * Invalid Python values that cannot be represented as a double will fail at the Python/C++ type boundary
             */
            double quantity;

            try
            {
                quantity = py::cast<double>(item.second);
            }
            catch(const py::cast_error&)
            {
                throw py::type_error("resource quantites must be numeric");
            }

            // Native DAR validation
            // DO NOT reproduce checks such as:
            /**
             * quantity < 0
             * std::isfinite(quantity)
             * valid resource-ame characters
             *   
             * Resource::set() already owns those invariants
             */

            const Status status = resource_set.Set(name, quantity);

            // Translate native DAR validation failures into the corresponding Python exception
            // Currently invalid names/quantities become: InvalidArgumentError
            ThrowStatus(status);
        }

        return ResourceRequest(std::move(resource_set));
    }

    // ResourceRequest Python binding
    void BindResources(py::module_& module)
    {
        py::class_<ResourceRequest> (module, "ResourceRequest")
        /**
         * Empty Request
         * 
         * Python:
         *   request = _dar.ResourceRequest()
         *   -> represent a task requiring no logical resources
         */
        .def(py::init<>())
        /**
         * Dictionary constructor
         * 
         * Python:
         *   request = _dar.ResourceRequest({
         * "CPU": 2.0, "GPU": 0.5})
         * 
         * A factory constructor is used because ResourceRequest itself intentially does not know anything about Python dictionaries
         */
        .def(py::init(
            [](const py::dict& resources)
            {
                return ResourceRequestFromDict(resources);
            }
        ), py::arg("resources"))
        // Python representation
        // Keep this representation simple from Phase 2
        // WE can expose richer resource introspection later if it becomes part of the public SDK contract
        .def(
            "__repr__",
            [](const ResourceRequest& request)
            {
                std::string representation = "ResourceRequest({";
                bool first = true;
                
                for(const auto& [name, quantity] : request.resources().values())
                {
                    if(!first)
                    {
                        representation += ',';
                    }
                    first = false;

                    representation += "'";
                    representation += name;
                    representation += "': ";
                    representation += std::to_string(quantity.ToDouble());
                }

                representation += "})";
                return representation;
            }
        );
    }
}