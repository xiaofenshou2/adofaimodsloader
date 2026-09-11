/*
 * ADOFAI Mod Bridge (C#)
 * 运行在游戏 IL2CPP 侧，通过 JNI/AndroidJavaClass 调用 Java ModHost。
 *
 * 职责：
 *   - 在"设置 -> 高级"面板注入"添加 Mod"按钮（仿"清除游戏数据"样式）
 *   - 点击 -> 打开文件选择器 -> 回调加载
 *
 * 注意：IL2CPP 下 UnityEngine.AndroidJavaClass 可调用 Java 静态方法。
 */

#if UNITY_ANDROID && !UNITY_EDITOR
using UnityEngine;
using UnityEngine.UI;

public class ModBridge : MonoBehaviour
{
    private static AndroidJavaClass  sModHostClass;
    private static AndroidJavaObject sModHost;

    /* 初始化：获取 Java ModHost 实例 */
    public static void Init()
    {
        using (var activity = new AndroidJavaClass("com.unity3d.player.UnityPlayer")
            .GetStatic<AndroidJavaObject>("currentActivity"))
        {
            sModHostClass = new AndroidJavaClass("com.adofai.mod.ModHost");
            sModHost      = new AndroidJavaObject("com.adofai.mod.ModHost", activity);
        }
    }

    /*
     * 在"高级设置"面板绘制"添加 Mod"按钮。
     * 用法：在游戏的高级选项 UI 初始化处调用 ModBridge.InjectButton(parentTransform)。
     *
     * parentTransform: 高级选项面板的 RectTransform（清除游戏数据按钮所在的父节点）
     */
    public static void InjectButton(Transform parentTransform)
    {
        if (parentTransform == null) return;

        // 克隆"清除游戏数据"按钮作为模板，保证样式一致
        Button template = FindClearDataButton(parentTransform);
        if (template != null)
        {
            Button addModBtn = Instantiate(template, parentTransform);
            addModBtn.onClick.RemoveAllListeners();
            addModBtn.onClick.AddListener(OnAddModClicked);

            Text txt = addModBtn.GetComponentInChildren<Text>();
            if (txt != null) txt.text = "添加 Mod";
        }
        else
        {
            // 找不到模板时退化为普通按钮
            GameObject go = new GameObject("AddModButton");
            go.transform.SetParent(parentTransform, false);
            Button btn = go.AddComponent<Button>();
            btn.onClick.AddListener(OnAddModClicked);
            Text txt = new GameObject("Text").AddComponent<Text>();
            txt.transform.SetParent(go.transform, false);
            txt.text = "添加 Mod";
        }
    }

    /* 点击"添加 Mod"：打开文件选择器 */
    private static void OnAddModClicked()
    {
        if (sModHost == null) Init();
        using (var activity = new AndroidJavaClass("com.unity3d.player.UnityPlayer")
            .GetStatic<AndroidJavaObject>("currentActivity"))
        {
            sModHost.CallStatic("openModFilePicker", activity);
        }
    }

    /* 文件选择回调（Java 侧 onActivityResult -> native -> 此处） */
    public static void OnFileSelected(string path)
    {
        Debug.Log("[ADOFAI Mod] file selected: " + path);
    }

    /* 查找"清除游戏数据"按钮作为样式模板 */
    private static Button FindClearDataButton(Transform root)
    {
        foreach (var btn in root.GetComponentsInChildren<Button>(true))
        {
            var t = btn.GetComponentInChildren<Text>();
            if (t != null && (t.text.Contains("清除") || t.text.Contains("Clear")))
                return btn;
        }
        return null;
    }
}
#endif
