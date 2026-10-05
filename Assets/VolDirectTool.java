/**
 * VolDirectTool — 以 system 身份直调 AudioManager 的音量直写工具。
 *
 * 背景：该固件上 shell 通道的绝对音量写入被阉（cmd media_session --set 假成功，
 * 经 TvService 走 cmd 又因 AppOps 包归属校验抛 SecurityException）。
 * 本工具经 TvService 的 app_process 运行，用 ActivityThread.systemMain()
 * 拿到 system context（包名归属 "android"/uid 1000 合法），直接调用
 * AudioManager.setStreamVolume —— 与小爱同学(voicecontrol)同一条生效路径。
 *
 * 用法：
 *   app_process <cacheDir> VolDirectTool set <stream> <index>
 *   app_process <cacheDir> VolDirectTool get
 * 输出走 logcat，tag = VolDirectTool。
 */
public class VolDirectTool {

    public static void main(String[] args) {
        try {
            log("start argc=" + args.length);
            // app_process 入口线程没有 Looper，创建 AudioManager 会抛
            // "Can't create handler inside thread..."，先准备主 Looper
            Class.forName("android.os.Looper")
                    .getMethod("prepareMainLooper")
                    .invoke(null);
            Class<?> atClass = Class.forName("android.app.ActivityThread");
            Object at = atClass.getMethod("systemMain").invoke(null);
            Object ctx = atClass.getMethod("getSystemContext").invoke(at);
            Object am = ctx.getClass().getMethod("getSystemService", String.class).invoke(ctx, "audio");

            String cmd = args.length > 0 ? args[0] : "get";
            if ("set".equals(cmd)) {
                int stream = args.length > 1 ? Integer.parseInt(args[1]) : 3;
                int index = args.length > 2 ? Integer.parseInt(args[2]) : -1;
                am.getClass()
                        .getMethod("setStreamVolume", int.class, int.class, int.class)
                        .invoke(am, stream, index, 1 /* FLAG_SHOW_UI，与小爱一致 */);
                log("SET stream=" + stream + " index=" + index + " invoked");
            } else {
                Object vol = am.getClass().getMethod("getStreamVolume", int.class).invoke(am, 3);
                Object max = am.getClass().getMethod("getStreamMaxVolume", int.class).invoke(am, 3);
                log("GET vol=" + vol + " max=" + max);
            }
        } catch (Throwable t) {
            try {
                StringBuilder sb = new StringBuilder("FAIL: ").append(t);
                Throwable c = t.getCause();
                int depth = 0;
                while (c != null && depth < 5) {
                    sb.append(" <- ").append(c);
                    c = c.getCause();
                    depth++;
                }
                Class<?> log = Class.forName("android.util.Log");
                log.getMethod("e", String.class, String.class)
                        .invoke(null, "VolDirectTool", sb.toString());
            } catch (Exception ignore) {
                // logcat 本身不可用时静默
            }
        }
    }

    private static void log(String msg) {
        try {
            Class<?> log = Class.forName("android.util.Log");
            log.getMethod("i", String.class, String.class).invoke(null, "VolDirectTool", msg);
        } catch (Exception ignore) {
            // 无 logcat 环境时静默
        }
    }
}
