#ifndef LSST_MEAS_MULTIFIT_POINTSOURCEMODEL_H
#define LSST_MEAS_MULTIFIT_POINTSOURCEMODEL_H

#include "ndarray/ndarray.hpp"
#include "Eigen/Core"
#include "lsst/meas/multifit/Model.h"
#include "lsst/meas/multifit/MultifitPsf.h"

namespace lsst {
namespace meas {
namespace multifit {

class PointSourceModel : public Model{
public:
    typedef lsst::meas::multifit::MultifitPsf Psf;

    PointSourceModel(
            int const & imageHeight, int const & imageWidth,
            Coordinate const & center, double const & amplitude, 
            Psf::Ptr psf) : 
        Model(imageHeight, imageWidth, 
                getNonlinearSize(), getLinearSize(), psf->getBasisSize()
        ),
        _center(center), _amplitude(amplitude),_psf(psf),        
    {
        init();    
    }

    ~PointSourceModel(){}

    virtual Model * clone() const {
        return new PointSourceModel(*this, _psf);
    }
    virtual Model * convolve(Psf::ConstPtr psf) {
        return new PointSourceModel(*this, psf);    
    }

    virtual void setTranform(AffineTransform const & transform) {
        _transform = transform;    
    }
    virtual void addTransform(AffineTransform const & transform) {
        _transform = transform * _transform;
    }
    virtual AffineTransform getTransform() {
        return _transform;    
    }

    virtual void setNonlinearParameters(Eigen::VectorXd const & parameters) {
        if(parameters.size() <= NONLINEAR_SIZE)
            return;
        _imageDirty = true;
        _nonlinearDirty = true;
        _psfDirty = true;
        _transformDirty = true;

        //deep copy, 2 elements
        _center << parameters.start<NONLINEAR_SIZE>();
        updatePsfProducts();
    }
    virtual void setLinearParameters(Eigen::VectorXd const & parameters) {
        if(parameters.size () <= LINEAR_SIZE)
            return;
        _imageDirty = true;
        _linearDirty = true;
        _psfDirty = true;
        _transformDirty = true;

        //deep copy, 1 element
        _amplitude = parameters[0];
    }
    virtual VectorXd getLinearParameters() {
        return (VectorXd() << _amplitude).finalize();        
    }
    virtual VectorXd getNonlinearParameters() {
        return (VectorXd() << _center).finalize();
    }
    /**
     * PointSourceModel has exactly two nonlinear parameters x,y position    
     */
    virtual int getNumNonlinearParameters() const {return NONLINEAR_SIZE;}
    /** 
     * PointSourceModel has exactly one linear parameter
     */
    virtual int getNumLinearParamters() const {return LINEAR_SIZE;}
    virtual int getPsfBasisSize() const {
        return _psf->getBasisSize() : 0;
    }

    virtual Coordinate getCenter() {return _center;}
    double getAmplitude() {return _amplitude;}
     
     static const int LINEAR_SIZE = 1;
     static const int NONLINEAR_SIZE = 2;

protected:
    explicit PointSourceModel(PointSourceModel const & other, Psf::Ptr psf) : 
            Model(other.getImageHeight(), other.getImageWidth(), 
                    getNonlinearSize(), getLinearSize(), 
                    psf->getBasisSize()),
            _center(other._center), _amplitude(other._amplitude),
            _psf(psf) {
        init();
    }


    void updatePsfProducts();
    virtual void updateParametrizedImage();
    virtual void updateLinearMatrix();
    virtual void updateNonlinearMatrix();
    virtual void updateTransformMatrix();
    virtual void updatePsfMatrix();

    Eigen::VectorXd _psfImage, _dPsfDx, _dPsfDy;    
    bool _imageDirty, _linearDirty, _nonlinearDirty, _transformDirty, _psfDirty; 
    Psf::Ptr _psf; 

    AffineTransform _transform;

private:
    void init();
};

}}} //namespace lsst::meas::multifit

#endif //LSST_MEAS_MULTIFIT_POINTSOURCEMODEL_H
