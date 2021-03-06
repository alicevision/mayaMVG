#include "MVGMayaUtil.hpp"
#include "meshroomMaya/core/MVGCamera.hpp"
#include "meshroomMaya/core/MVGLog.hpp"
#include "meshroomMaya/core/MVGMesh.hpp"
#include "meshroomMaya/qt/MVGPanelWrapper.hpp"
#include "meshroomMaya/qt/MVGMainWidget.hpp"
#include "meshroomMaya/maya/context/MVGMoveManipulator.hpp"
#include "meshroomMaya/maya/context/MVGContext.hpp"
#include "meshroomMaya/maya/context/MVGContextCmd.hpp"
#include <maya/MGlobal.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MSelectionList.h>
#include <maya/MQtUtil.h>
#include <maya/MItSelectionList.h>
#include <maya/MFnSingleIndexedComponent.h>

namespace meshroomMaya
{

namespace
{ // empty namespace

MVGProjectWrapper* getProjectWrapper()
{
    QWidget* menuLayout = MVGMayaUtil::getMVGMenuLayout();
    if(!menuLayout)
        return NULL;
    MVGMainWidget* mainWidget = menuLayout->findChild<MVGMainWidget*>("mvgMainWidget");
    if(!mainWidget)
        return NULL;
    return &mainWidget->getProjectWrapper();
}

} // empty namespace

static void selectionChangedCB(void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;
    MSelectionList list;
    MGlobal::getActiveSelectionList(list);
    MItSelectionList selectionIt(list);
    MDagPath path;
    MObject component;
    QStringList selectedCameras;
    QStringList selectedMeshes;
    std::set<int> selectedParticles;
    bool particleSelectionChanged = false;
    
    for (; !selectionIt.isDone(); selectionIt.next())
    {
        selectionIt.getDagPath(path, component);
        path.extendToShape();
        if(!path.isValid())
            continue;
                
        switch(path.apiType())
        {
            case MFn::kCamera:
                selectedCameras.push_back(path.fullPathName().asChar());
                break;
            case MFn::kMesh:
                selectedMeshes.push_back(path.fullPathName().asChar());
                break;
            case MFn::kParticle:
                if(!component.isNull())
                {
                    particleSelectionChanged = true;
                    const MFnSingleIndexedComponent cpts(component);
                    MIntArray indices;
                    cpts.getElements(indices);
                    for(unsigned int i = 0; i < indices.length(); ++i)
                        selectedParticles.insert(indices[i]);
                }
                break;
            default:
                break;
        }
    }

    // Synchronisation between IHM/Maya selection
    if(!project->getActiveSynchro())
        return;
    // Compare IHM selection to Maya selection
    if(!selectedCameras.empty())
    {
        QStringList IHMSelectedCamera = project->getSelectedCameras();
        IHMSelectedCamera.sort();
        selectedCameras.sort();
        if(IHMSelectedCamera != selectedCameras)
            project->addCamerasToIHMSelection(selectedCameras, true);
    }
    else
        project->clearCameraSelection();
    if(!selectedMeshes.empty())
    {
        QStringList IHMSelectedMeshes = project->getSelectedMeshes();
        IHMSelectedMeshes.sort();
        selectedMeshes.sort();
        if(IHMSelectedMeshes != selectedMeshes)
            project->addMeshesToIHMSelection(selectedMeshes, true);
    }
    else
        project->clearMeshSelection();
    if(list.length() == 0 || particleSelectionChanged)
        project->updateParticleSelection(selectedParticles);
}

static void currentContextChangedCB(void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;
    MString context;
    CHECK(MVGMayaUtil::getCurrentContext(context))
    project->setCurrentContext(MQtUtil::toQString(context));
}

static void sceneChangedCB(void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;
    MGlobal::executePythonCommand("from meshroomMaya import window;\n"
                                  "window.mvgReloadPanels()");
    project->loadExistingProject();
    MString cmd;
    cmd.format("^1s -e -rebuild ^2s", MVGContextCmd::name, MVGContextCmd::instanceName);
    MGlobal::executeCommand(cmd);
}

static void newSceneCB(void*)
{
    MVGMayaUtil::deleteMVGWindow();
}

static void undoCB(void*)
{
    // TODO : rebuild only the modified mesh
    // TODO : don't rebuild on action that don't modify any mesh
    MString redoName;
    MVGMayaUtil::getRedoName(redoName);
    // Don't rebuild on selection
    int spaceIndex = redoName.index(' ');
    MString cmdName = redoName.substring(0, spaceIndex - 1);
    if(cmdName != "select" && cmdName != "miCreateDefaultPresets")
    {
        MString cmd;
        cmd.format("^1s -e -rebuild ^2s", MVGContextCmd::name, MVGContextCmd::instanceName);
        MGlobal::executeCommand(cmd);
    }
}

static void redoCB(void*)
{
    MString undoName;
    MVGMayaUtil::getUndoName(undoName);
    // Don't rebuild if selection action
    int spaceIndex = undoName.index(' ');
    MString cmdName = undoName.substring(0, spaceIndex - 1);
    if(cmdName != "select" && cmdName != "miCreateDefaultPresets")
    {
        MString cmd;
        cmd.format("^1s -e -rebuild ^2s", MVGContextCmd::name, MVGContextCmd::instanceName);
        MGlobal::executeCommand(cmd);
    }
}

/**
 * @brief Listen to the Maya nodes creation to update the list of Meshes.
**/
static void nodeAddedCB(MObject& node, void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;

    switch(node.apiType())
    {
        case MFn::kMesh:
        {
            MFnDagNode fn(node);
            if(fn.isIntermediateObject())
                return;
            MVGMesh mesh(node);
            if(mesh.isValid())
                project->addMeshToUI(mesh.getDagPath());
            break;
        }
        case MFn::kSet:
        {
            if(MVGProject::isMVGCameraSet(node))
                project->addCameraSetToUI(node);
        }
        default:
            break;
    }
}

static void nodeRemovedCB(MObject& node, void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;

    switch(node.apiType())
    {
        case MFn::kMesh:
        {
            MFnDagNode fn(node);
            if(fn.isIntermediateObject())
                return;
            MVGMesh mesh(node);
            if(!mesh.isValid())
                return;
            project->removeMeshFromUI(mesh.getDagPath());
            // TODO : remove only this mesh from cache
            MString cmd;
            cmd.format("^1s -e -rebuild ^2s", MVGContextCmd::name, MVGContextCmd::instanceName);
            MGlobal::executeCommand(cmd);
            break;
        }
        default:
            break;
    }
}

static void modelEditorChangedCB(void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;
    for(int i = 0; i < project->getPanelList()->count(); ++i)
    {
        MVGPanelWrapper* panel = static_cast<MVGPanelWrapper*>(project->getPanelList()->at(i));
        if(!panel)
            return;
        panel->emitIsPointCloudDisplayedChanged();
    }
}

static void linearUnitChanged(void*)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;
    project->emitCurrentUnitChanged();
}

static void modeChangedCB(void* /*data*/)
{
    MVGProjectWrapper* project = getProjectWrapper();
    if(!project)
        return;
    int editMode, moveMode;
    MString cmd;
    cmd.format("^1s -q -editMode ^2s", MVGContextCmd::name, MVGContextCmd::instanceName);
    MGlobal::executeCommand(cmd, editMode);
    project->setEditMode(editMode);
    cmd.format("^1s -q -moveMode ^2s", MVGContextCmd::name, MVGContextCmd::instanceName);
    MGlobal::executeCommand(cmd, moveMode);
    project->setMoveMode(moveMode);
}
} // namespace
