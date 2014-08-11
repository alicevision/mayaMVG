#include "mayaMVG/core/MVGProject.h"
#include "mayaMVG/core/MVGCamera.h"
#include "mayaMVG/core/MVGLog.h"
#include "mayaMVG/io/cameraIO.h"
#include "mayaMVG/io/pointCloudIO.h"
#include "mayaMVG/maya/MVGMayaUtil.h"
#include <maya/MFnTransform.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MDagModifier.h>
#include <maya/MFnTypedAttribute.h>
#include <third_party/stlplus3/filesystemSimplified/file_system.hpp>

namespace mayaMVG {

std::string MVGProject::_CLOUD = "mvgPointCloud";
std::string MVGProject::_MESH = "mvgMesh";
std::string MVGProject::_PROJECT = "mayaMVG";

MString	MVGProject::_PROJECTPATH = "project";

std::string MVGProject::_cameraRelativeDirectory = stlplus::folder_append_separator("outIncremental")
                                        + stlplus::folder_append_separator("SfM_output")
                                        + stlplus::folder_append_separator("cameras");
std::string MVGProject::_imageRelativeDirectory = stlplus::folder_append_separator("outIncremental")
                                        + stlplus::folder_append_separator("SfM_output")
                                        + stlplus::folder_append_separator("images");
std::string MVGProject::_cameraRelativeFile = stlplus::folder_append_separator("outIncremental")
                                        + stlplus::folder_append_separator("SfM_output")
                                        + "views.txt";
std::string MVGProject::_pointCloudRelativeFile = stlplus::folder_append_separator("outIncremental")
                                        + stlplus::folder_append_separator("SfM_output")
                                        + stlplus::folder_append_separator("clouds")
                                        + "calib.ply";

MVGProject::MVGProject(const std::string& name)
	: MVGNodeWrapper(name)
{
}

MVGProject::MVGProject(const MDagPath& dagPath)
	: MVGNodeWrapper(dagPath)
{
}

MVGProject::~MVGProject()
{
}

// virtual
bool MVGProject::isValid() const
{
	if(!_dagpath.isValid() || (_dagpath.apiType()!=MFn::kTransform))
		return false;
	return true;
}

// static
MVGProject MVGProject::create(const std::string& name)
{
	MStatus status;
	MFnTransform fn;
	MDagPath path;
	
	// root node
	MObject transform = fn.create(MObject::kNullObj, &status);
	// root/cameras
	fn.create(transform, &status);
	fn.setName("cameras");
	// root/clouds
	fn.create(transform, &status);
	fn.setName("clouds");
	// root/meshes
	fn.create(transform, &status);
	fn.setName("meshes");
	CHECK(status)

	MDagPath::getAPathTo(transform, path);
	MVGProject project(path);
	project.setName(name);
    
    // Add root attributes
    MDagModifier dagModifier;
    MFnTypedAttribute tAttr;
    MObject projectAttr = tAttr.create(_PROJECTPATH, "project", MFnData::kString);
    dagModifier.addAttribute(path.node(), projectAttr);
    dagModifier.doIt();
    
	return project;
}

bool MVGProject::load(const std::string& projectDirectoryPath)
{
    if(!isProjectDirectoryValid(projectDirectoryPath))
        return false;
    
    // Create project node if deleted
    MDagPath path;
    if(!MVGMayaUtil::getDagPathByName(_PROJECT.c_str(), path))
        create(_PROJECT.c_str());

    // Clean MAYA cameras and pointCloud (under mayaMVG node)
    MDagPath camerasDagPath;
    MVGMayaUtil::getDagPathByName("cameras", camerasDagPath);
    for(int i = camerasDagPath.childCount(); i > 0; --i)
    {
        MObject child = camerasDagPath.child(i - 1);
        MGlobal::deleteNode(child);
    }
    
    MDagPath pointCloudDagPath;
    MVGMayaUtil::getDagPathByName("clouds", pointCloudDagPath);
    for(int i = pointCloudDagPath.childCount(); i > 0; --i)
    {
        MObject child = pointCloudDagPath.child(i - 1);
        MGlobal::deleteNode(child);
    }

    // Load new elements
	if(!loadCameras(projectDirectoryPath))
		return false;
	if(!loadPointCloud(projectDirectoryPath))
		return false;
    
	return true;
}

bool MVGProject::loadCameras(const std::string& projectDirectoryPath)
{
    std::string cameraFile = stlplus::folder_append_separator(projectDirectoryPath)
                                + _cameraRelativeFile;
    std::string imageDirectory = stlplus::folder_append_separator(projectDirectoryPath)
                                + _imageRelativeDirectory;
    std::string cameraDirectory = stlplus::folder_append_separator(projectDirectoryPath)
                                + _cameraRelativeDirectory;
	return readCameras(cameraFile, imageDirectory, cameraDirectory);
}

bool MVGProject::loadPointCloud(const std::string& projectDirectoryPath)
{
    std::string pointCloudFile = stlplus::folder_append_separator(projectDirectoryPath)
                                    + _pointCloudRelativeFile;
	return readPointCloud(pointCloudFile);
}

std::string MVGProject::moduleDirectory() const
{
	return MVGMayaUtil::getModulePath().asChar();
}

std::string MVGProject::projectDirectory() const
{
    MString directory;
    
    // FIX ME : When opening a scene from Maya menu, mayaMVG root node is destroyed
    // Retrive it by its name
    MDagPath path;
    MVGMayaUtil::getDagPathByName(_PROJECT.c_str(), path);
    MVGMayaUtil::getStringAttribute(path.node(), _PROJECTPATH, directory);
    
    return (directory.length() > 0) ? directory.asChar() : "";
}

void MVGProject::setProjectDirectory(const std::string& directory) const
{
     // FIX ME : When opening a scene from Maya menu, mayaMVG root node is destroyed
    // Retrive it by its name
    MDagPath path;
    MVGMayaUtil::getDagPathByName(_PROJECT.c_str(), path);
    MVGMayaUtil::setStringAttribute(path.node(), _PROJECTPATH, directory.c_str());
}

bool MVGProject::isProjectDirectoryValid(const std::string& projectDirectoryPath) const
{
    // Camera file
    std::string cameraFile = stlplus::folder_append_separator(projectDirectoryPath)
                                + _cameraRelativeFile;
    std::ifstream infile(cameraFile.c_str());
	if (!infile.is_open())
	{
		LOG_ERROR("Camera file not found (" << cameraFile << ")")
		return false;
	}
    
    // Cloud file
    std::string pointCloudFile = stlplus::folder_append_separator(projectDirectoryPath)
                                + _pointCloudRelativeFile;
    Ply ply;
	if(!ply.open(pointCloudFile))
	{
		LOG_ERROR("Point cloud file not found (" << pointCloudFile << ")")
		ply.close();
		return false;
	}
    
    return true;
}

std::vector<MVGCamera> MVGProject::cameras() const
{
	return MVGCamera::list();
}

std::vector<MVGPointCloud> MVGProject::pointClouds() const
{
    std::vector<MVGPointCloud> list;
	MDagPath path;
	MItDependencyNodes it(MFn::kParticle);
	for (; !it.isDone(); it.next()) {
		MFnDependencyNode fn(it.thisNode());
		MDagPath::getAPathTo(fn.object(), path);
		MVGPointCloud cloud(path);
		if(cloud.isValid())
			list.push_back(cloud);
	}
	return list;
}

}	//mayaMVG