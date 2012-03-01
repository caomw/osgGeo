#include <iostream>
#include <stdlib.h>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgGeo/Horizon3D>

int main(int argc, char **argv)
{
    double undef = 999999.0;
    osgGeo::Vec2i size(1000, 1000);

    osg::ref_ptr<osg::DoubleArray> depthValsPtr =
            new osg::DoubleArray(size.x()*size.y());

    osg::DoubleArray &depthVals = *depthValsPtr;

    for(int i = 0; i < size.x(); ++i)
        for(int j = 0; j < size.y(); ++j)
        {
            float val = float(std::rand()) / RAND_MAX * 3;
            float zFactor = 50.0 / size.x();
            depthVals[i*size.y()+j] = sin(double(i+val)/50.0) * sin(double(j+val)/50.0) *zFactor;
        }

    for(int i = 100; i < 200; ++i)
        for(int j = 0; j < size.y(); ++j)
        {
            if(i % 2 == 1)
                depthVals[i*size.y()+j] = undef;
        }

    std::vector<osg::Vec2d> coords;
    coords.push_back(osg::Vec2d(0, 0));
    coords.push_back(osg::Vec2d(0, 1));
    coords.push_back(osg::Vec2d(1, 0));

    osg::ref_ptr<osgGeo::Horizon3DNode> horizon3d = new osgGeo::Horizon3DNode();
    horizon3d->setCornerCoords(coords);
    horizon3d->setSize(size);
    horizon3d->setMaxDepth(undef);
    horizon3d->setDepthArray(depthValsPtr.get());

    osgViewer::Viewer viewer;
    // add the stats handler
    viewer.addEventHandler(new osgViewer::StatsHandler);
    viewer.setSceneData(horizon3d.get());
    return viewer.run();
}

