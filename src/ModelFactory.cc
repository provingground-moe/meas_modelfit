#include <lsst/meas/multifit/ModelFactory.h>
#include <lsst/meas/multifit/Model.h>

namespace multifit = lsst::meas::multifit;

multifit::ModelFactory::ConstPtr multifit::ModelFactory::lookupFactory(
    std::string const & name
) {
    RegistryMap::const_iterator i = _registry.find(name);
    if (i == _registry.end()) {
        throw LSST_EXCEPT(
            lsst::pex::exception::InvalidParameterException,
            (boost::format("No ModelFactory associated with name '%s'.") % name).str()
        );
    }
    return i->second;
}

bool multifit::ModelFactory::registerFactory(
    std::string const & name, 
    ModelFactory::ConstPtr const & factory
) {
    std::pair<RegistryMap::iterator,bool> result = 
        _registry.insert(std::make_pair(name,factory));
    return result.second;
}


