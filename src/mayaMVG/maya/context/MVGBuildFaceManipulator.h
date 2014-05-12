#pragma once

#include "mayaMVG/core/MVGCamera.h"
#include <maya/MPxManipulatorNode.h>
#include <maya/MDagPath.h>
#include <vector>

class M3dView;

namespace mayaMVG {

class MVGBuildFaceManipulator: public MPxManipulatorNode
{
	public:
		MVGBuildFaceManipulator();
		virtual ~MVGBuildFaceManipulator();
		
	public:
		static void * creator();
		static MStatus initialize();

	public:
		virtual void postConstructor();
		virtual void draw(M3dView&, const MDagPath&, M3dView::DisplayStyle, M3dView::DisplayStatus);
		virtual MStatus doPress(M3dView &view);
		virtual MStatus doRelease(M3dView &view);
		virtual MStatus doMove(M3dView &view, bool& refresh);

		// viewport 2.0 manipulator draw overrides
		virtual void preDrawUI(const M3dView&);
		virtual void drawUI(MHWRender::MUIDrawManager&,	const MHWRender::MFrameContext&) const;

		MVGCamera getMVGCamera();
		MVGCamera getMVGCamera(M3dView&);
		
		void createFace3d(M3dView& view, std::vector<MPoint> facePoints);
		

	public:
		static MTypeId			_id;
		std::vector<MPoint>		_wpoints;
		MPoint					_mousePoint;
		MPoint					_lastPoint;
		MDagPath				_lastCameraPath;
		bool					_drawEnabled;
		bool				_connectFace;
		bool				_computeLastPoint;
};

} // mayaMVG