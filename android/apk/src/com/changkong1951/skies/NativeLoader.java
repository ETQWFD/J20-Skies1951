package com.changkong1951.skies;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

// NativeActivity + a tiny hidden EditText so the game can raise the REAL system
// soft keyboard and read/write the system clipboard (for pasting server URLs).
public class NativeLoader extends NativeActivity {
    static { System.loadLibrary("main"); }

    // Driven by the EditText watcher on the UI thread, consumed by the game loop.
    public static native void nativeImeChar(int codePoint);
    public static native void nativeImeKey(int key);   // 8=backspace 66=enter

    private EditText edit;
    private InputMethodManager imm;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private String prev = "";
    private boolean syncing = false;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        edit = new EditText(this);
        edit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        edit.setImeOptions(EditorInfo.IME_ACTION_DONE);
        edit.setSingleLine(true);
        edit.setBackgroundColor(0x00000000);
        edit.setTextColor(0x00000000);
        FrameLayout.LayoutParams lp =
                new FrameLayout.LayoutParams(1, 1, Gravity.START | Gravity.TOP);
        ViewGroup content = (ViewGroup) findViewById(android.R.id.content);
        if (content != null) content.addView(edit, lp);
        // Keep it VISIBLE (1px, fully transparent) at all times: some IMEs
        // refuse to open for an INVISIBLE/GONE view, while a 1px transparent
        // field is unseen yet reliably focusable.
        edit.setVisibility(View.VISIBLE);
        edit.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            public void onTextChanged(CharSequence s, int a, int b, int c) {}
            public void afterTextChanged(Editable e) {
                if (syncing) return;
                String now = e.toString();
                int min = Math.min(prev.length(), now.length()), common = 0;
                while (common < min && prev.charAt(common) == now.charAt(common)) common++;
                for (int i = 0, n = prev.length() - common; i < n; i++) nativeImeKey(8);
                int i = common;
                while (i < now.length()) {
                    int cp = now.codePointAt(i);
                    nativeImeChar(cp);
                    i += Character.charCount(cp);
                }
                prev = now;
            }
        });
        edit.setOnEditorActionListener((v, actionId, ev) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) { nativeImeKey(66); return true; }
            return false;
        });
    }

    private void setEditSync(final String s) {
        syncing = true;
        edit.setText(s == null ? "" : s);
        prev = s == null ? "" : s;
        edit.setSelection(edit.getText().length());
        syncing = false;
    }

    // ---- called from C (render thread); hop to the UI thread as needed ----
    public void imeOpen(final String current) {
        ui.post(() -> {
            setEditSync(current);
            edit.setVisibility(View.VISIBLE);
            edit.requestFocus();
            if (imm != null) imm.showSoftInput(edit, InputMethodManager.SHOW_FORCED);
        });
    }
    public void imeClose() {
        ui.post(() -> {
            if (imm != null) imm.hideSoftInputFromWindow(edit.getWindowToken(), 0);
            edit.clearFocus();
            edit.setVisibility(View.INVISIBLE);
        });
    }
    public void imeSet(final String s) { ui.post(() -> setEditSync(s)); }
    public String imeText() {
        final String[] r = { edit.getText().toString() };
        return r[0];
    }

    public String cfgDir() { return getFilesDir().getAbsolutePath(); }

    public String clipGet() {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null && cm.hasPrimaryClip() && cm.getPrimaryClip() != null
                && cm.getPrimaryClip().getItemCount() > 0) {
            CharSequence c = cm.getPrimaryClip().getItemAt(0).coerceToText(this);
            return c == null ? "" : c.toString();
        }
        return "";
    }
    public void clipSet(String t) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("skies", t == null ? "" : t));
    }
}
