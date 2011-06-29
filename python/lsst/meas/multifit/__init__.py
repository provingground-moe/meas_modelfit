# 
# LSST Data Management System
# Copyright 2008, 2009, 2010, 2011 LSST Corporation.
# 
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the LSST License Statement and 
# the GNU General Public License along with this program.  If not, 
# see <http://www.lsstcorp.org/LegalNotices/>.
#
import lsst.afw.geom.ellipses

from .multifitLib import (
    BaseEvaluator,
    Grid,
    Definition,
    Evaluator,
    Evaluation,
    SourceMeasurement,
    ModelBasis,
    ShapeletModelBasis,
    ProfileFunction,
    CompoundShapeletModelBasis,
    CompoundShapeletBuilder,
    version,
    makeSourceMeasurement
    )
from . import multifitLib
from . import definition
from . import grid

import os
import eups

Position = lsst.afw.geom.Point2D
Ellipticity = lsst.afw.geom.ellipses.ConformalShear
Radius = lsst.afw.geom.ellipses.TraceRadius
EllipseCore = lsst.afw.geom.ellipses.Separable[(Ellipticity, Radius)];
CompoundShapeletBuilder.ComponentVector = multifitLib.CompoundShapelet_ComponentVector
CompoundShapeletModelBasis.ComponentVector = multifitLib.CompoundShapelet_ComponentVector

del multifitLib

