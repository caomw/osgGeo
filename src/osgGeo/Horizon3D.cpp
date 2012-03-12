/* osgGeo - A collection of geoscientific extensions to OpenSceneGraph.
Copyright 2011 dGB Beheer B.V.

osgGeo is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
*/


#include <osg/Geode>
#include <osg/Geometry>

#include <osgGeo/Horizon3D>
#include <osgGeo/LayeredTexture>
#include <osgGeo/Palette>

#include <iostream>

namespace osgGeo
{

class Horizon3DTesselatorBase : public OpenThreads::Thread
{
public:
    struct CommonData
    {
        CommonData(const Vec2i& fullSize_,
                   osg::DoubleArray *depthVals_,
                   float maxDepth_,
                   const std::vector<osg::Vec2d> &coords_);

        Vec2i fullSize; // full size of the horizon
        osg::DoubleArray *depthVals;
        float maxDepth;
        std::vector<osg::Vec2d> coords;
        Vec2i maxSize; // reference size of the tile
        osg::Vec2d iInc, jInc; // increments of realworld coordinates along the grid dimensions
        int numHTiles, numVTiles; // number of tiles of horizon within
        osg::ref_ptr<osgGeo::LayeredTexture> laytex;
    };

    struct Result
    {
        osg::ref_ptr<osg::Node> triangles, pointsLines;
        int resLevel;
    };

    Horizon3DTesselatorBase(const CommonData &data);

    const std::vector<Result> &getResults() const;

protected:
    const CommonData &_data;
    std::vector<Result> _results;
};

const std::vector<Horizon3DTesselatorBase::Result> &Horizon3DTesselatorBase::getResults() const
{
    return _results;
}

class Horizon3DTesselator : public Horizon3DTesselatorBase
{
public:
    /**
      * This struct contains information that identifies a tile within horizon
      * and which is used to build it.
      */
    struct Job
    {
        Job(int hI, int vI, int resLevel_) :
            hIdx(hI), vIdx(vI), resLevel(resLevel_) {}

        // indexes of the tile within the horizon
        int hIdx, vIdx;

        // resolution level of horizon 1, 2, 3 ... which means that every
        // first, second, fourth ... points are displayed and all the rest
        // are discarded
        int resLevel;
    };

    Horizon3DTesselator(const CommonData &data);

    void addJob(const Job &job);
    virtual void run();

    bool isUndef(double val);
    double mkUndef();

private:
    std::vector<Job> _jobs;
};

Horizon3DTesselatorBase::CommonData::CommonData(const Vec2i& fullSize_,
                                            osg::DoubleArray *depthVals_,
                                            float maxDepth_,
                                            const std::vector<osg::Vec2d> &coords_)
{
    fullSize = fullSize_;
    depthVals = depthVals_;
    maxDepth = maxDepth_;
    coords = coords_;

    maxSize = Vec2i(256, 256);

    iInc = (coords[2] - coords[0]) / (fullSize.x() - 1);
    jInc = (coords[1] - coords[0]) / (fullSize.y() - 1);

    numHTiles = ceil(float(fullSize.x()) / maxSize.x());
    numVTiles = ceil(float(fullSize.y()) / maxSize.y());
}

Horizon3DTesselatorBase::Horizon3DTesselatorBase(const CommonData &data) :
    _data(data)
{
}

Horizon3DTesselator::Horizon3DTesselator(const CommonData &data) :
    Horizon3DTesselatorBase(data)
{
}

void Horizon3DTesselator::addJob(const Job &job)
{
    _jobs.push_back(job);
}

bool Horizon3DTesselator::isUndef(double val)
{
    return val >= _data.maxDepth;
}

double Horizon3DTesselator::mkUndef()
{
    return _data.maxDepth;
}

namespace
{

int index(int i, int j, int size)
{
    return i * size + j;
}

static const char *shaderVertSource = {
    "// microshader - colors a fragment based on its position\n"
    "#version 330 compatibility\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n"
};
static const char *shaderFragSource = {
    "#version 330 compatibility\n"
    "uniform vec4 colour;\n"
    "void main(void)\n"
    "{\n"
    "    gl_FragColor = colour;\n"
    "}\n"
};

}

void Horizon3DTesselator::run()
{
    const CommonData &data = _data;

    for(int jId = 0; jId < _jobs.size(); ++jId)
    {
        const Job &job = _jobs[jId];

        // compression rate. 1 means no compression
        const int compr = (int) pow( (float) 2, job.resLevel);
        int iCompr = compr;
        int jCompr = compr;

        int realHSize = data.maxSize.x() / iCompr;
        int realVSize = data.maxSize.y() / jCompr;

        const int hSize = job.hIdx < (data.numHTiles - 1) ?
                    (realHSize + 1) : (data.fullSize.x() - data.maxSize.x() * (data.numHTiles - 1)) / iCompr;
        const int vSize = job.vIdx < (data.numVTiles - 1) ?
                    (realVSize + 1) : ((data.fullSize.y() - data.maxSize.y() * (data.numVTiles - 1))) / jCompr;

        // work out texture coords of a tile quad
        std::vector<LayeredTexture::TextureCoordData> tcData;
        osg::ref_ptr<osg::StateSet> stateset;
        {
            int left = job.hIdx * data.maxSize.x();
            int right = job.hIdx * data.maxSize.x() + (hSize - 1) * iCompr;
            int top = job.vIdx * data.maxSize.y();
            int bottom = job.vIdx * data.maxSize.y() + (vSize - 1) * jCompr;

            stateset = data.laytex->createCutoutStateSet(osg::Vec2(top, left), osg::Vec2(bottom, right), tcData);
            stateset->ref();
        }

        std::vector<LayeredTexture::TextureCoordData>::iterator tcit = tcData.begin();
        osg::Vec2 textureTileStep = tcit->_tc11 - tcit->_tc00;

        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array(hSize * vSize);
        osg::ref_ptr<osg::Vec2Array> tCoords = new osg::Vec2Array(hSize * vSize);

        // first we construct an array of vertices which is just a grid
        // of depth values.
        for(int i = 0; i < hSize; ++i)
            for(int j = 0; j < vSize; ++j)
            {
                const int iGlobal = job.hIdx * data.maxSize.x() + i * iCompr;
                const int jGlobal = job.vIdx * data.maxSize.y() + j * jCompr;
                osg::Vec2d hor = data.coords[0] + data.iInc * iGlobal + data.jInc * jGlobal;
                (*vertices)[i*vSize+j] = osg::Vec3(
                            hor.x(),
                            hor.y(),
                            data.depthVals->at(iGlobal*data.fullSize.y()+jGlobal)
                            );
                (*tCoords)[i*vSize+j] = tcit->_tc00 + osg::Vec2(float(j) / (vSize - 1) * textureTileStep.x(),
                                                                float(i) / (hSize - 1) * textureTileStep.y());
            }

        // the following loop populates array of indices that make up
        // triangles out of vertices data, each grid cell has 2 triangles.
        // If a vertex is undefined then triangle that contains it is
        // discarded. Also normals are calculated for each triangle to be
        // later used for calculating normals per vertex
        osg::ref_ptr<osg::DrawElementsUInt> indices =
                new osg::DrawElementsUInt(GL_TRIANGLES);

        osg::ref_ptr<osg::Vec3Array> triangleNormals =
                new osg::Vec3Array((hSize - 1) * (vSize - 1) * 2);

        for(int i = 0; i < hSize - 1; ++i)
            for(int j = 0; j < vSize - 1; ++j)
            {
                const int i00 = i*vSize+j;
                const int i10 = (i+1)*vSize+j;
                const int i01 = i*vSize+(j+1);
                const int i11 = (i+1)*vSize+(j+1);

                const osg::Vec3 v00 = (*vertices)[i00];
                const osg::Vec3 v10 = (*vertices)[i10];
                const osg::Vec3 v01 = (*vertices)[i01];
                const osg::Vec3 v11 = (*vertices)[i11];

                if(isUndef(v10.z()) || isUndef(v01.z()))
                    continue;

                // first triangle
                if(!isUndef(v00.z()))
                {
                    indices->push_back(i00);
                    indices->push_back(i10);
                    indices->push_back(i01);
                }

                // second triangle
                if(!isUndef(v11.z()))
                {
                    indices->push_back(i10);
                    indices->push_back(i01);
                    indices->push_back(i11);
                }

                // calculate triangle normals
                osg::Vec3 norm1 = (v01 - v00) ^ (v10 - v00);
                norm1.normalize();
                (*triangleNormals)[(i*(vSize-1)+j)*2] = norm1;

                osg::Vec3 norm2 = (v10 - v11) ^ (v01 - v11);
                norm2.normalize();
                (*triangleNormals)[(i*(vSize-1)+j)*2+1] = norm2;
            }

        // The following loop calculates normals per vertex. Because
        // each vertex might be shared between many triangles(up to 6)
        // we find out which triangles this particular vertex is shared
        // and then compute the average of normals per triangle.
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array(hSize * vSize);

        osg::Vec3 triNormCache[6];

        for(int i = 0; i < hSize; ++i)
            for(int j = 0; j < vSize; ++j)
            {
                int k = 0;
                for(int l = 0; l < k; ++l)
                    triNormCache[l] = osg::Vec3();

                const int vSizeT = vSize - 1;

                // 3
                if((i < hSize - 1) && (j < vSize - 1))
                {
                    triNormCache[k++] = (*triangleNormals)[(i*vSizeT+j)*2];
                }

                // 4, 5
                if(i > 0 && j < vSize - 1)
                {
                    triNormCache[k++] = (*triangleNormals)[((i-1)*vSizeT+j)*2];
                    triNormCache[k++] = (*triangleNormals)[((i-1)*vSizeT+j)*2+1];
                }

                // 1, 2
                if(j > 0 && i < hSize - 1)
                {
                    triNormCache[k++] = (*triangleNormals)[(i*vSizeT+j-1)*2];
                    triNormCache[k++] = (*triangleNormals)[(i*vSizeT+j-1)*2+1];
                }

                // 6
                if(i > 0 && j > 0)
                {
                    triNormCache[k++] = (*triangleNormals)[((i-1)*vSizeT+j-1)*2+1];
                }

                if(k > 0)
                {
                    osg::Vec3 norm;
                    for(int l = 0; l < k; ++l)
                        norm += triNormCache[l];

                    norm.normalize();

                    (*normals)[i*vSize+j] = -norm;
                }
            }

        osg::ref_ptr<osg::Vec3Array> points = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec3Array> lines = new osg::Vec3Array;

        const bool diaglines = false;

        for(int i = 1; i < hSize - 1; ++i)
            for(int j = 1; j < vSize; ++j)
            {
                osg::Vec3 udf(0.0, 0.0, mkUndef());
                osg::Vec3 p1 = (*vertices)[index(i-1, j-1, vSize)];
                osg::Vec3 p2 = (*vertices)[index(i, j-1, vSize)];
                osg::Vec3 p3 = (*vertices)[index(i+1, j-1, vSize)];
                osg::Vec3 p4 = (*vertices)[index(i-1, j, vSize)];
                osg::Vec3 p5 = (*vertices)[index(i, j, vSize)];
                osg::Vec3 p6 = (*vertices)[index(i+1, j, vSize)];
                osg::Vec3 p7 = (j == vSize - 1) ? udf : (*vertices)[index(i-1, j+1, vSize)];
                osg::Vec3 p8 = (j == vSize - 1) ? udf : (*vertices)[index(i, j+1, vSize)];
                osg::Vec3 p9 = (j == vSize - 1) ? udf : (*vertices)[index(i+1, j+1, vSize)];

                if(isUndef(p5.z()))
                {
                    if(!isUndef(p6.z()) && !isUndef(p2.z()))
                    {

                    }
                }
                else if(!isUndef(p2.z()) && !isUndef(p3.z()) && !isUndef(p6.z()))
                {

                }
                else if(!isUndef(p2.z()))
                {
                    if ( !isUndef(p3.z()) )
                         {}//make triangle 5,2,3;
                    else if ( !isUndef(p6.z()) )
                         {}//make triangle 5,2,6
                    else if ( isUndef(p1.z()) && isUndef(p4.z()) )
                    {
                         lines->push_back(p5);
                         lines->push_back(p2);
                    }
                }
                else if(!isUndef(p6.z()))
                {
                    if(!isUndef(p3.z()))
                        {} //make triangle 5,6,3
                    else if(isUndef(p8.z()) && isUndef(p9.z()))
                    {
                        lines->push_back(p5);
                        lines->push_back(p6);
                    }
                }
                else if(!isUndef(p2.z()) && isUndef(p5.z()) && isUndef(p1.z()))
                {
                    lines->push_back(p5);
                    lines->push_back(p2);
                }
                else if(diaglines && !isUndef(p3.z()))
                {
                    lines->push_back(p5);
                    lines->push_back(p3);
                }
                else if(isUndef(p4.z()) && isUndef(p8.z()))
                {
                    points->push_back(p5);
                }
            }

        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
        osg::Vec4 colour(1.0f, 1.0f, 1.0f, 1.0f);
        colors->push_back(colour);

        osg::ref_ptr<osg::Group> group = new osg::Group();

        Result result;
        result.resLevel = job.resLevel;

        // triangles
        {
            osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
            geom->setVertexArray(vertices.get());
            geom->setNormalArray(normals.get());
            geom->setNormalBinding(osg::Geometry::BIND_PER_VERTEX);
//            geom->setColorArray(colors.get());
//            geom->setColorBinding(osg::Geometry::BIND_OVERALL);
            geom->setTexCoordArray(tcit->_textureUnit, tCoords.get());

            geom->addPrimitiveSet(indices.get());
            geom->setStateSet(stateset);

            osg::ref_ptr<osg::Geode> geode = new osg::Geode;
            geode->addDrawable(geom.get());
            result.triangles = geode;
        }

        if(lines->size() > 0 || points->size() > 0)
        {
            osg::ref_ptr<osg::Geode> geode = new osg::Geode;

            if(lines->size() > 0)
            {
                osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
                geom->setVertexArray(lines.get());
                geom->setColorArray(colors.get());
                geom->setColorBinding(osg::Geometry::BIND_OVERALL);
                geom->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, lines->size()));
                geode->addDrawable(geom.get());
            }
            if(points->size() > 0)
            {
                osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
                geom->setVertexArray(points.get());
                geom->setColorArray(colors.get());
                geom->setColorBinding(osg::Geometry::BIND_OVERALL);
                geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, points->size()));

                geode->addDrawable(geom.get());
            }

            result.pointsLines = geode;
/*
            // Temporary disable shaders for lines and points as they affect triangles
            // as well, possibly a bug in OSG
            osg::StateSet *ss = geode->getOrCreateStateSet();

            osg::Program* program = new osg::Program;
            program->setName( "microshader" );
            program->addShader( new osg::Shader( osg::Shader::VERTEX, shaderVertSource ) );
            program->addShader( new osg::Shader( osg::Shader::FRAGMENT, shaderFragSource ) );

            ss->addUniform(new osg::Uniform("colour", colour));
            ss->setAttributeAndModes( program, osg::StateAttribute::ON );
            */
        }

        _results.push_back(result);
    }
}

Horizon3DNode::Horizon3DNode()
    : osg::Node(),
    _needsUpdate(true)
{
    init();
}

Horizon3DNode::Horizon3DNode(const Horizon3DNode& other,
                             const osg::CopyOp& op) :
    osg::Node(other, op),
    _needsUpdate(true)
{
    init();
    // TODO Proper copy
}

void Horizon3DNode::init()
{
    setNumChildrenRequiringUpdateTraversal(getNumChildrenRequiringUpdateTraversal()+1);
    _texture = new osgGeo::LayeredTexture();
}

void Horizon3DNode::setSize(const Vec2i& size)
{
    _size = size;
}

const Vec2i& Horizon3DNode::getSize() const
{
    return _size;
}

void Horizon3DNode::setDepthArray(osg::Array *arr)
{
    _array = arr;
    _needsUpdate = true;
}

const osg::Array *Horizon3DNode::getDepthArray() const
{
    return _array;
}

osg::Array *Horizon3DNode::getDepthArray()
{
    return _array;
}

void Horizon3DNode::setCornerCoords(const std::vector<osg::Vec2d> &coords)
{
    _cornerCoords = coords;
}

std::vector<osg::Vec2d> Horizon3DNode::getCornerCoords() const
{
    return _cornerCoords;
}

bool Horizon3DNode::isUndef(double val)
{
    return val >= getMaxDepth();
}

osg::Image *Horizon3DNode::makeElevationTexture()
{
    osg::ref_ptr<osg::Image> img = new osg::Image();
    osgGeo::Vec2i sz = getSize();
    const int depth = 1;
    img->allocateImage(sz.y(), sz.x(), depth, GL_RGB, GL_UNSIGNED_BYTE);

    osg::DoubleArray *depthVals = dynamic_cast<osg::DoubleArray*>(getDepthArray());
    double min = +999999;
    double max = -999999;
    for(int j = 0; j < sz.y(); ++j)
    {
        for(int i = 0; i < sz.x(); ++i)
        {
            const double val = depthVals->at(j * sz.x() + i);
            if(isUndef(val))
                continue;
            min = std::min(val, min);
            max = std::max(val, max);
        }
    }

    Palette p;

    GLubyte *ptr = img->data();
    for(int j = 0; j < sz.y(); ++j)
    {
        for(int i = 0; i < sz.x(); ++i)
        {
            const double val = depthVals->at(j * sz.x() + i);

            osg::Vec3 c = p.get(val, min, max);
            *(ptr + 0) = GLubyte(c.x() * 256.0);
            *(ptr + 1) = GLubyte(c.y() * 256.0);
            *(ptr + 2) = GLubyte(c.z() * 256.0);
            ptr += 3;
        }
    }
    return img.release();
}

void Horizon3DNode::updateGeometry()
{
    if(getDepthArray()->getType() != osg::Array::DoubleArrayType)
        return;

    Horizon3DTesselator::CommonData data(getSize(),
                                         dynamic_cast<osg::DoubleArray*>(getDepthArray()),
                                         getMaxDepth(),
                                         getCornerCoords());

    int lastId = _texture->addDataLayer();
    _texture->setDataLayerOrigin( lastId, osg::Vec2f(0.0f,0.0f) );
    _texture->setDataLayerScale( lastId, osg::Vec2f(1.0f,1.0f) );
    _texture->setDataLayerImage( lastId, makeElevationTexture() );
    _texture->setDataLayerTextureUnit( lastId, lastId );

    data.laytex = _texture.get();

    const int numCPUs = OpenThreads::GetNumberOfProcessors();
    std::vector<Horizon3DTesselator*> threads(numCPUs);
    for(int i = 0; i < numCPUs; ++i)
        threads[i] = new Horizon3DTesselator(data);

    const int resolutionsNum = 3;
    _nodes.resize(resolutionsNum);

    int currentThread = 0;
    for(int resLevel = 0; resLevel < resolutionsNum; ++resLevel)
    {
        // clean up nodes arrays
        _nodes[resLevel].resize(0);

        for(int hIdx = 0; hIdx < data.numHTiles; ++hIdx)
        {
            for(int vIdx = 0; vIdx < data.numVTiles; ++vIdx)
            {
                threads[currentThread]->addJob(Horizon3DTesselator::Job(hIdx, vIdx, resLevel));
                currentThread++;
                if(currentThread == numCPUs)
                    currentThread = 0;
            }
        }
    }

    for(int i = 0; i < numCPUs; ++i)
        threads[i]->startThread();

    for(int i = 0; i < numCPUs; ++i)
        threads[i]->join();

    for(int i = 0; i < numCPUs; ++i)
    {
        const std::vector<Horizon3DTesselator::Result> &nodes = threads[i]->getResults();
        for(int j = 0; j < nodes.size(); ++j)
        {
            Horizon3DTesselator::Result res = nodes[j];
            _nodes[res.resLevel].push_back(res.triangles.get());
            if(res.pointsLines.get())
                _nodes[res.resLevel].push_back(res.pointsLines.get());
        }
    }

    for(int i = 0; i < numCPUs; ++i)
        delete threads[i];

    _needsUpdate = false;
}

bool Horizon3DNode::needsUpdate() const
{
    return _needsUpdate;
}

void Horizon3DNode::setMaxDepth(float val)
{
    _maxDepth = val;
}

float Horizon3DNode::getMaxDepth() const
{
    return _maxDepth;
}

void Horizon3DNode::setLayeredTexture(LayeredTexture *texture)
{
    _texture = texture;
}

LayeredTexture *Horizon3DNode::getLayeredTexture()
{
    return _texture;
}

const LayeredTexture *Horizon3DNode::getLayeredTexture() const
{
    return _texture;
}

void Horizon3DNode::traverse(osg::NodeVisitor &nv)
{
    if ( nv.getVisitorType()==osg::NodeVisitor::UPDATE_VISITOR )
    {
        if ( needsUpdate() )
            updateGeometry();
    }
    else if(nv.getVisitorType()==osg::NodeVisitor::CULL_VISITOR)
    {
        const float distance = nv.getDistanceToViewPoint(getBound().center(), true);

        const std::vector<osg::Vec2d> coords = getCornerCoords();
        const float iDen = ((coords[2] - coords[0]) / getSize().x()).length();
        const float jDen = ((coords[1] - coords[0]) / getSize().y()).length();

        const float k = std::min(iDen, jDen);
        const float threshold1 = k * 2000.0;
        const float threshold2 = k * 8000.0;

        if(distance < threshold1)
        {
            for(int i = 0; i <  _nodes[0].size(); ++i)
               _nodes[0].at(i)->accept(nv);
        }
        else if(distance < threshold2)
        {
            for(int i = 0; i < _nodes[1].size(); ++i)
                _nodes[1].at(i)->accept(nv);
        }
        else
        {
            for(int i = 0; i < _nodes[2].size(); ++i)
                _nodes[2].at(i)->accept(nv);
        }
    }
}

}
