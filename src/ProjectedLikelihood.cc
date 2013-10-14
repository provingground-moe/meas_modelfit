// -*- lsst-c++ -*-
/*
 * LSST Data Management System
 * Copyright 2008-2013 LSST Corporation.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
#include <algorithm>
#include <limits>
#include <numeric>

#include "boost/format.hpp"
#include "boost/make_shared.hpp"
#include "ndarray/eigen.h"

#include "lsst/afw/image/Calib.h"
#include "lsst/afw/detection/FootprintArray.cc"  // yes .cc; see the file for an explanation
#include "lsst/meas/multifit/ProjectedLikelihood.h"

namespace lsst { namespace meas { namespace multifit {

namespace {

typedef std::vector< shapelet::MultiShapeletMatrixBuilder<Pixel> > MatrixBuilderVector;

/*
 * Function intended for use with std algorithms to compute the cumulative sum
 * of the number of pixels in a sequence of EpochFootprints
 */
int componentPixelSum(int partialNumPixels, CONST_PTR(EpochFootprint) const &epochImagePtr) {
    return partialNumPixels + epochImagePtr->footprint.getArea();
}

/*
 * Return a vector of MultiShapeletMatrixBuilders, with one for each MultiShapeletBasis in the input vector,
 * using the pixel region defined by the given Footprint and the given shapelet PSF approximation.
 */
MatrixBuilderVector makeMatrixBuilders(
    Model::BasisVector const & basisVector,
    shapelet::MultiShapeletFunction const & psf,
    afw::detection::Footprint const & footprint,
    bool useApproximateExp
) {
    MatrixBuilderVector result;
    result.reserve(basisVector.size());
    ndarray::Array<Pixel,1,1> x = ndarray::allocate(footprint.getArea());
    ndarray::Array<Pixel,1,1> y = ndarray::allocate(footprint.getArea());
    int n = 0;
    for (
        afw::detection::Footprint::SpanList::const_iterator i = footprint.getSpans().begin();
        i != footprint.getSpans().end();
        ++i
    ) {
        for (afw::geom::Span::Iterator j = (**i).begin(); j != (**i).end(); ++j, ++n) {
            x[n] = j->getX();
            y[n] = j->getY();
        }
    }
    for (Model::BasisVector::const_iterator k = basisVector.begin(); k != basisVector.end(); ++k) {
        result.push_back(
            shapelet::MultiShapeletMatrixBuilder<Pixel>(**k, psf, x, y, useApproximateExp)
        );
    }
    return result;
}

/*
 *  Flatten image and variance arrays from a MaskedImage using a footprint, and transform
 *  the variance into weights
 */
void setupArrays(
    afw::image::MaskedImage<Pixel> const & image,
    afw::detection::Footprint const & footprint,
    ndarray::Array<Pixel,1,1> const & data,
    ndarray::Array<Pixel,1,1> const & weights,
    double fluxScaling,
    bool usePixelWeights
) {
    afw::detection::flattenArray(footprint, image.getImage()->getArray(), data, image.getXY0());
    afw::detection::flattenArray(footprint, image.getVariance()->getArray(), weights, image.getXY0());
    // Convert from variance to weights (1/sigma); this is actually the usual inverse-variance
    // weighting, because we implicitly square it later.
    weights.asEigen<Eigen::ArrayXpr>() = weights.asEigen<Eigen::ArrayXpr>().sqrt().inverse();
    if (!usePixelWeights) {
        // We want a single number for the weights, so we use the geometric mean, as that
        // preserves the determinant of the (diagonal) pixel covariance matrix.
        weights.asEigen<Eigen::ArrayXpr>().setConstant(
            std::pow(weights.asEigen<Eigen::ArrayXpr>().prod(), 1.0 / footprint.getArea())
        );
    }
    data.asEigen<Eigen::ArrayXpr>() *= weights.asEigen<Eigen::ArrayXpr>();
    weights.asEigen<Eigen::ArrayXpr>() *= fluxScaling;
}

} // anonymous

EpochFootprint::EpochFootprint(
    afw::detection::Footprint const &footprint_,
    afw::image::Exposure<Pixel> const &exposure_,
    shapelet::MultiShapeletFunction const & psf_
) :
    footprint(footprint_),
    exposure(afw::image::Exposure<Pixel>(exposure_, false)),
    psf(psf_)
{}

class ProjectedLikelihood::Impl {
public:

    class Epoch {
    public:

        Epoch(int nPix_, MatrixBuilderVector matrixBuilders_, afw::geom::AffineTransform transform_) :
            nPix(nPix_), matrixBuilders(matrixBuilders_), transform(transform_) {}

        int nPix;
        MatrixBuilderVector matrixBuilders;
        afw::geom::AffineTransform transform;
    };

    Impl() : scratch(afw::geom::ellipses::Quadrupole(), afw::geom::Point2D()) {}

    ndarray::Array<Pixel,1,1> weights;
    std::vector<Epoch> epochs;
    Model::EllipseVector ellipses;
    afw::geom::ellipses::Ellipse scratch;
};

ProjectedLikelihood::ProjectedLikelihood(
    PTR(Model) model,
    ndarray::Array<Scalar const,1,1> const & fixed,
    afw::image::Wcs const & fitWcs,
    afw::image::Calib const & fitCalib,
    afw::coord::Coord const & sourceSkyPos,
    std::vector<PTR(EpochFootprint)> const & epochFootprintList,
    ProjectedLikelihoodControl const & ctrl
) : Likelihood(model, fixed), _impl(new Impl()) {
    int totPixels = std::accumulate(epochFootprintList.begin(), epochFootprintList.end(),
                                    0, componentPixelSum);
    _data = ndarray::allocate(totPixels);
    _impl->weights = ndarray::allocate(totPixels);
    _impl->epochs.reserve(epochFootprintList.size());
    _impl->ellipses = model->makeEllipseVector();
    afw::geom::AffineTransform fitToSky = fitWcs.linearizePixelToSky(sourceSkyPos, afw::geom::radians);
    double fitFluxMag0 = fitCalib.getFluxMag0().first;
    int dataOffset = 0;
    for (
        std::vector<PTR(EpochFootprint)>::const_iterator imPtrIter = epochFootprintList.begin();
        imPtrIter != epochFootprintList.end(); ++imPtrIter
    ) {
        afw::geom::AffineTransform skyToCalexp =
            (*imPtrIter)->exposure.getWcs()->linearizeSkyToPixel(sourceSkyPos, afw::geom::radians);
        double calexpFluxMag0 = (**imPtrIter).exposure.getCalib()->getFluxMag0().first;
        int nPix = (**imPtrIter).footprint.getArea();
        int dataEnd = dataOffset + nPix;
        _impl->epochs.push_back(
            Impl::Epoch(
                nPix,
                makeMatrixBuilders(model->getBasisVector(), (**imPtrIter).psf, (**imPtrIter).footprint,
                                   ctrl.useApproximateExp),
                skyToCalexp * fitToSky
            )
        );
        setupArrays(
            (**imPtrIter).exposure.getMaskedImage(),
            (**imPtrIter).footprint,
            _data[ndarray::view(dataOffset, dataEnd)],
            _impl->weights[ndarray::view(dataOffset, dataEnd)],
            fitFluxMag0 / calexpFluxMag0,
            ctrl.usePixelWeights
        );
    }
}

ProjectedLikelihood::ProjectedLikelihood(
    PTR(Model) model,
    ndarray::Array<Scalar const,1,1> const & fixed,
    afw::image::Wcs const & fitWcs,
    afw::image::Calib const & fitCalib,
    afw::coord::Coord const & sourceSkyPos,
    afw::image::Exposure<Pixel> const & exposure,
    afw::detection::Footprint const & footprint,
    shapelet::MultiShapeletFunction const & psf,
    ProjectedLikelihoodControl const & ctrl
) : Likelihood(model, fixed), _impl(new Impl()) {
    int totPixels = footprint.getArea();
    _data = ndarray::allocate(totPixels);
    _impl->weights = ndarray::allocate(totPixels);
    _impl->ellipses = model->makeEllipseVector();
    afw::geom::AffineTransform fitToSky = fitWcs.linearizePixelToSky(sourceSkyPos, afw::geom::radians);
    double fitFluxMag0 = fitCalib.getFluxMag0().first;
    afw::geom::AffineTransform skyToCalexp =
        exposure.getWcs()->linearizeSkyToPixel(sourceSkyPos, afw::geom::radians);
    double calexpFluxMag0 = exposure.getCalib()->getFluxMag0().first;
    _impl->epochs.push_back(
        Impl::Epoch(
            totPixels,
            makeMatrixBuilders(model->getBasisVector(), psf, footprint, ctrl.useApproximateExp),
            skyToCalexp * fitToSky
        )
    );
    setupArrays(
        exposure.getMaskedImage(), footprint, _data, _impl->weights,
        fitFluxMag0 / calexpFluxMag0, ctrl.usePixelWeights
    );
}

ProjectedLikelihood::~ProjectedLikelihood() {}

void ProjectedLikelihood::computeModelMatrix(
    ndarray::Array<Pixel,2,-1> const & modelMatrix,
    ndarray::Array<Scalar const,1,1> const & nonlinear
) const {
    getModel()->writeEllipses(nonlinear.begin(), _fixed.begin(), _impl->ellipses.begin());
    int dataOffset = 0;
    for (
        std::vector<Impl::Epoch>::const_iterator i = _impl->epochs.begin();
        i != _impl->epochs.end();
        ++i
    ) {
        int dataEnd = dataOffset + i->nPix;
        int amplitudeOffset = 0;
        for (std::size_t j = 0; j < _impl->ellipses.size(); ++j) {
            _impl->scratch = _impl->ellipses[j].transform(i->transform);
            int amplitudeEnd = amplitudeOffset + i->matrixBuilders[j].getBasisSize();
            i->matrixBuilders[j].build(
                modelMatrix[ndarray::view(dataOffset, dataEnd)(amplitudeOffset, amplitudeEnd)],
                _impl->scratch
            );
            amplitudeOffset = amplitudeEnd;
        }
        dataOffset = dataEnd;
    }
}

}}} // namespace lsst::meas::multifit