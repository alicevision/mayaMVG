#include "mayaMVG/maya/context/MVGContext.h"
#include "mayaMVG/maya/context/MVGCreateManipulator.h"
#include "mayaMVG/maya/context/MVGMoveManipulator.h"
#include "mayaMVG/maya/MVGMayaUtil.h"
#include "mayaMVG/qt/MVGQt.h"
#include <maya/MQtUtil.h>

namespace mayaMVG
{

MVGContext::MVGContext()
    : _filter((QObject*)MVGMayaUtil::getMVGWindow(), this)
    , _filterLV((QObject*)MVGMayaUtil::getMVGViewportLayout("mvgLPanel"), this)
    , _filterRV((QObject*)MVGMayaUtil::getMVGViewportLayout("mvgRPanel"), this)
    , _editMode(eModeMove)
    , _manipUtil(this)
{
    setTitleString("MVG tool");
}

void MVGContext::toolOnSetup(MEvent& event)
{
    updateManipulators();
    _manipUtil.rebuildAllMeshesCacheFromMaya();
    _manipUtil.rebuild();
}

void MVGContext::toolOffCleanup()
{
    deleteManipulators();
    MPxContext::toolOffCleanup();
}

void MVGContext::getClassName(MString& name) const
{
    name.set("mayaMVGTool");
}

void MVGContext::updateManipulators()
{
    MString currentContext;
    MVGMayaUtil::getCurrentContext(currentContext);
    if(currentContext != "mayaMVGTool1")
        return;
    // delete all manipulators
    deleteManipulators();
    // then add a new one, depending on edit mode
    MStatus status;
    MObject manipObject;
    switch(_editMode)
    {
        case eModeCreate:
        {
            MVGCreateManipulator* manip = static_cast<MVGCreateManipulator*>(
                MPxManipulatorNode::newManipulator("MVGCreateManipulator", manipObject, &status));
            if(!status || !manip)
                return;
            manip->setManipUtil(&_manipUtil);
            break;
        }
        case eModeMove:
        {
            MVGMoveManipulator* manip = static_cast<MVGMoveManipulator*>(
                MPxManipulatorNode::newManipulator("MVGMoveManipulator", manipObject, &status));
            if(!status || !manip)
                return;
            _manipUtil.resetTemporaryData();
            manip->setManipUtil(&_manipUtil);
            break;
        }
        default:
            return;
    }
    if(status)
        addManipulator(manipObject);
}

bool MVGContext::eventFilter(QObject* obj, QEvent* e)
{
    // key pressed
    if(e->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyevent = static_cast<QKeyEvent*>(e);
        if(!keyevent->isAutoRepeat())
        {
            switch(keyevent->key())
            {
                case Qt::Key_F:
                {
                    M3dView view = M3dView::active3dView();
                    MDagPath cameraDPath;
                    view.getCamera(cameraDPath);
                    MVGCamera camera(cameraDPath);
                    camera.resetZoomAndPan();
                    return true;
                }
                case Qt::Key_C:
                    _editMode = eModeCreate;
                    updateManipulators();
                    return true;
                case Qt::Key_Control:
                case Qt::Key_Shift:
                case Qt::Key_Escape:
                    updateManipulators();
                    return true;
                default:
                    break;
            }
        }
    }
    // key released
    else if(e->type() == QEvent::KeyRelease)
    {
        QKeyEvent* keyevent = static_cast<QKeyEvent*>(e);
        if(!keyevent->isAutoRepeat())
        {
            switch(keyevent->key())
            {
                case Qt::Key_C:
                    _editMode = eModeMove;
                    updateManipulators();
                    return true;
                case Qt::Key_Control:
                case Qt::Key_Shift:
                case Qt::Key_Escape:
                    updateManipulators();
                    return true;
                default:
                    break;
            }
        }
    }
    // mouse button pressed
    else if(e->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* mouseevent = static_cast<QMouseEvent*>(e);
        // middle button: initialize camera pan
        if((mouseevent->button() & Qt::MidButton))
        {
            if(_eventData.cameraPath.isValid())
            {
                MVGCamera camera(_eventData.cameraPath);
                _eventData.onPressMousePos = mouseevent->pos();
                _eventData.onPressCameraHPan = camera.getHorizontalPan();
                _eventData.onPressCameraVPan = camera.getVerticalPan();
                _eventData.isDragging = true;
                return true;
            }
        }
    }
    // mouse button moved
    else if(e->type() == QEvent::MouseMove)
    {
        if(_eventData.isDragging)
        {
            MVGCamera camera(_eventData.cameraPath);
            // compute pan offset
            QMouseEvent* mouseevent = static_cast<QMouseEvent*>(e);
            QPointF offset_screen = _eventData.onPressMousePos - mouseevent->posF();
            const double viewport_width = qobject_cast<QWidget*>(obj)->width();
            QPointF offset = (offset_screen / viewport_width) * camera.getHorizontalFilmAperture() *
                             camera.getZoom();
            camera.setPan(_eventData.onPressCameraHPan + offset.x(),
                          _eventData.onPressCameraVPan - offset.y());
            return true;
        }
    }
    // mouse button released
    else if(e->type() == QEvent::MouseButtonRelease)
    {
        _eventData.isDragging = false;
        //		return true;
    }
    // mouse wheel rolled
    else if(e->type() == QEvent::Wheel)
    {
        if(_eventData.cameraPath.isValid())
        {
            // compute & set zoom value
            QMouseEvent* mouseevent = static_cast<QMouseEvent*>(e);
            QWheelEvent* wheelevent = static_cast<QWheelEvent*>(e);
            MVGCamera camera(_eventData.cameraPath);
            QWidget* widget = qobject_cast<QWidget*>(obj);
            const double viewportWidth = widget->width();
            const double viewportHeight = widget->height();
            static const double wheelStep = 1.15;
            const double previousZoom = camera.getZoom();
            double newZoom =
                wheelevent->delta() > 0 ? previousZoom / wheelStep : previousZoom * wheelStep;
            camera.setZoom(std::max(newZoom, 0.0001));
            const double scaleRatio = newZoom / previousZoom;
            // compute & set pan offset
            QPointF center_ratio(0.5, 0.5 * viewportHeight / viewportWidth);
            QPointF mouse_ratio_center = (center_ratio - (mouseevent->posF() / viewportWidth));
            QPointF mouse_maya_center =
                mouse_ratio_center * camera.getHorizontalFilmAperture() * previousZoom;
            QPointF mouseAfterZoo_maya_center = mouse_maya_center * scaleRatio;
            QPointF offset = mouse_maya_center - mouseAfterZoo_maya_center;
            camera.setPan(camera.getHorizontalPan() - offset.x(),
                          camera.getVerticalPan() + offset.y());
            return true;
        }
    }
    else if(e->type() == QEvent::Leave)
    {
        // Reset camera path which is only set in QEvent::Enter (event not detected
        // for QML
        // view)
        _eventData.cameraPath = MDagPath();
    }
    // mouse enters widget's boundaries
    else if(e->type() == QEvent::Enter)
    {
        // check if we are entering an MVG panel
        QVariant panelName = obj->property("mvg_panel");
        if(panelName.type() != QVariant::Invalid)
        {
            // find & register the associated camera path
            MVGMayaUtil::getCameraInView(_eventData.cameraPath,
                                         MQtUtil::toMString(panelName.toString()));
            if(_eventData.cameraPath.isValid())
            {
                // automagically set focus on this MVG panel
                MVGMayaUtil::setFocusOnView(MQtUtil::toMString(panelName.toString()));
                return true;
            }
        }
    }
    return false;
}

MVGEditCmd* MVGContext::newCmd()
{
    return (MVGEditCmd*)newToolCommand();
}

} // namespace
