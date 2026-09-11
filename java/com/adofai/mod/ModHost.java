package com.adofai.mod;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * ADOFAI Mod 宿主（Java 侧）
 *
 * 职责：
 *   1. 在"设置 -> 高级"里提供"添加 Mod"入口（由 C#/Unity 侧调用 openModFilePicker）
 *   2. 通过 SAF 打开文件选择器，仅接受 .zip
 *   3. 将选中的 zip 拷贝到私有目录，做根目录纯净度检查
 *   4. 加载根目录的 .dll（需 HybridCLR）/ .so（原版即可用）
 */
public class ModHost {

    private static final String PKG = "com.adofai.mod";
    private static final int    REQ_PICK_ZIP = 9001;

    private final Context context;
    private static ModHost sInstance;

    public ModHost(Context ctx) {
        this.context = ctx.getApplicationContext();
        sInstance = this;
    }

    /* ===================== ① 文件选择 ===================== */

    /** 由 Unity/C# 侧调用：打开文件选择器 */
    public static void openModFilePicker(Activity activity) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("application/zip");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"application/zip"});
        activity.startActivityForResult(intent, REQ_PICK_ZIP);
    }

    /** Activity 中重写 onActivityResult 后调用此方法 */
    public static void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQ_PICK_ZIP || resultCode != Activity.RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null) return;

        ModHost host = sInstance;
        if (host == null) return;

        /* 拷贝到私有目录 */
        File dst = host.copyUriToPrivate(uri);
        if (dst == null) {
            notifySystem(host.context, "Mod 加载失败", "无法读取所选文件");
            return;
        }

        /* 校验后缀（非 zip 直接拒绝） */
        if (!dst.getName().toLowerCase().endsWith(".zip")) {
            notifySystem(host.context, "不支持的格式", "仅支持 .zip 格式的 Mod 文件");
            dst.delete();
            return;
        }

        /* 根目录纯净度检查 */
        if (!isZipRootClean(dst)) {
            notifySystem(host.context, "Mod 无法加载", "请检查 zip 根目录：不应包含文件夹");
            dst.delete();
            return;
        }

        /* 交给 native 加载 */
        host.loadMod(dst);
    }

    /* ===================== ② zip 根目录纯净度检查 ===================== */

    /**
     * 规则：zip 根目录不得包含任何文件夹。
     *   - "xxx"            -> 根目录文件（合法）
     *   - "xxx/"           -> 空目录（非法）
     *   - "xxx/yyy"        -> 子目录文件（非法）
     */
    public static boolean isZipRootClean(File zip) {
        try (ZipFile zf = new ZipFile(zip)) {
            Enumeration<? extends ZipEntry> en = zf.entries();
            while (en.hasMoreElements()) {
                String name = en.nextElement().getName();
                int slash = name.indexOf('/');
                if (slash == -1) {
                    continue; // 根目录文件：合法
                } else if (slash == name.length() - 1) {
                    return false; // "xxx/" 空目录
                } else {
                    return false; // "xxx/yyy" 套文件夹
                }
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /* ===================== ③ 加载 ===================== */

    private void loadMod(File zip) {
        /* 1) 优先尝试加载根目录 .so（原版 IL2CPP 可直接 dlopen） */
        boolean soLoaded = extractAndLoadSo(zip);

        /* 2) 尝试加载根目录 .dll（需 HybridCLR，否则提示） */
        boolean dllLoaded = extractAndLoadDll(zip);

        if (!soLoaded && !dllLoaded) {
            notifySystem(context, "Mod 加载失败", "未找到有效的 .dll 或 .so 文件");
        } else {
            notifySystem(context, "Mod 已加载", "成功加载 Mod");
        }

        /* 回调 native，让 C 侧也能感知 */
        onFileSelected(zip.getAbsolutePath());
    }

    /** 解压并 dlopen 根目录的所有 .so */
    private boolean extractAndLoadSo(File zip) {
        // 实现：遍历 zip 根目录条目 -> 写出到 files/mods/ -> System.load(soPath)
        // IL2CPP 可通过 System.load 加载 native so，so 内需导出 Init()
        return false; // TODO: 遍历 + System.load 实现
    }

    /** 解压并通过 HybridCLR 加载根目录的 .dll */
    private boolean extractAndLoadDll(File zip) {
        // 若游戏已集成 HybridCLR：
        //   byte[] dll = readEntry(zip, "Mod.dll");
        //   HomologousImageRuntime.LoadMetadata(dll);
        //   Assembly ass = Assembly.Load(dll);
        //   ass.GetType("MyMod")?.GetMethod("Init")?.Invoke(null, null);
        notifySystem(context, "提示", ".dll Mod 需要游戏集成 HybridCLR 方可加载（当前为原版 IL2CPP，建议改用 .so）");
        return false;
    }

    /* ===================== ④ 系统通知 ===================== */

    public static void notifySystem(Context ctx, String title, String msg) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            android.app.NotificationChannel channel =
                    new android.app.NotificationChannel(PKG, "ADOFAI Mod",
                            android.app.NotificationManager.IMPORTANCE_DEFAULT);
            android.app.NotificationManager nm =
                    (android.app.NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm != null) nm.createNotificationChannel(channel);
        }
        android.app.NotificationCompat.Builder b =
                new android.app.NotificationCompat.Builder(ctx, PKG)
                        .setSmallIcon(android.R.drawable.ic_dialog_info)
                        .setContentTitle(title)
                        .setContentText(msg)
                        .setAutoCancel(true);
        android.app.NotificationManager nm =
                (android.app.NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.notify((int) System.currentTimeMillis(), b.build());

        Toast.makeText(ctx, title + ": " + msg, Toast.LENGTH_LONG).show();
    }

    /* ===================== 工具 ===================== */

    private File copyUriToPrivate(Uri uri) {
        try {
            InputStream in = context.getContentResolver().openInputStream(uri);
            File dir = new File(context.getFilesDir(), "mods");
            if (!dir.exists()) dir.mkdirs();
            File dst = new File(dir, "selected_" + System.currentTimeMillis() + ".zip");
            OutputStream out = new FileOutputStream(dst);
            byte[] buf = new byte[8192];
            int len;
            while ((len = in.read(buf)) > 0) out.write(buf, 0, len);
            in.close();
            out.close();
            return dst;
        } catch (Exception e) {
            return null;
        }
    }

    /* ===================== JNI ===================== */

    static { System.loadLibrary("adofai_mod"); }

    /** 由 native 回调（jni_entry.c） */
    public static native void onFileSelected(String path);

    /** 供 C 侧调用：弹出通知（对应 C 里的 java_notify） */
    public static void notify(String title, String msg) {
        notifySystem(sInstance != null ? sInstance.context : null, title, msg);
    }

    /** HybridCLR 加载入口（由 native load_one_dll 调用） */
    public boolean loadDllHybridCLR(String dllPath) {
        return extractAndLoadDll(new File(dllPath));
    }
}
