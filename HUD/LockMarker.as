package {
    import flash.display.MovieClip;
    import flash.display.Shape;
    import flash.display.StageAlign;
    import flash.display.StageScaleMode;
    import flash.events.Event;

    [SWF(width="1280", height="720", frameRate="60", backgroundColor="#000000")]
    public class LockMarker extends MovieClip {
        private var marker:Shape = new Shape();
        private var missedFrames:int = 0;
        private var markerColor:uint = 0xFFFFFF;

        public function LockMarker() {
            mouseEnabled = false;
            mouseChildren = false;
            marker.visible = false;
            addChild(marker);
            // Dark backing keeps the colored diamond visible on bright surfaces.
            drawDiamond(4, 0x101010, 0.85);
            drawDiamond(1.5, 0xFFFFFF, 1);
            addEventListener(Event.ADDED_TO_STAGE, onStage);
            addEventListener(Event.ENTER_FRAME, onFrame);
        }
        private function drawDiamond(width:Number, color:uint, opacity:Number):void {
            marker.graphics.lineStyle(width, color, opacity);
            marker.graphics.moveTo(0, -10);
            marker.graphics.lineTo(10, 0);
            marker.graphics.lineTo(0, 10);
            marker.graphics.lineTo(-10, 0);
            marker.graphics.lineTo(0, -10);
        }
        private function onStage(event:Event):void {
            stage.scaleMode = StageScaleMode.NO_SCALE;
            stage.align = StageAlign.TOP_LEFT;
        }
        private function onFrame(event:Event):void {
            // Hide if the plugin stops sending updates rather than leaving a stale marker.
            if (++missedFrames > 3) marker.visible = false;
        }
        private function channel(value:Number):uint {
            return uint(Math.round(Math.max(0, Math.min(1, isFinite(value) ? value : 1)) * 255));
        }
        public function updateMarker(nx:Number, ny:Number, show:Boolean, size:Number, red:Number, green:Number, blue:Number):void {
            const color:uint = (channel(red) << 16) | (channel(green) << 8) | channel(blue);
            if (color != markerColor) {
                markerColor = color;
                marker.graphics.clear();
                drawDiamond(4, 0x101010, 0.85);
                drawDiamond(1.5, markerColor, 1);
            }
            missedFrames = 0;
            marker.visible = show && stage != null && isFinite(nx) && isFinite(ny)
                && nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1;
            if (!marker.visible) return;
            marker.x = nx * stage.stageWidth;
            marker.y = ny * stage.stageHeight;
            marker.scaleX = marker.scaleY = Math.max(0.5, Math.min(2, size));
        }
    }
}
