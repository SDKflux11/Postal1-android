package org.libsdl.app;

import android.app.Activity;
import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Handles automatic first-launch unpacking of bundled game assets (either assets/game_data.zip
 * or loose assets/res/ + POSTAL.INI) into getExternalFilesDir(null) before SDL native libraries start.
 */
public final class FirstRunAssetExtractor {

    private static final String GAME_DATA_ZIP = "game_data.zip";

    private FirstRunAssetExtractor() {}

    /**
     * Returns true if game assets are already present in getExternalFilesDir(null),
     * or if no bundled assets (game_data.zip / res) exist inside the APK's assets/.
     */
    public static boolean areAssetsReady(Context context) {
        File extDir = context.getExternalFilesDir(null);
        if (extDir == null) {
            return true;
        }
        syncIniCaseVariants(extDir);

        File upperIni = new File(extDir, "POSTAL.INI");
        File resDir = new File(extDir, "res");
        if (upperIni.exists() && resDir.isDirectory()) {
            return true;
        }

        // If external files are missing, only show the first-launch extractor if the APK bundles assets
        return !hasBundledAssets(context.getAssets());
    }

    private static void syncIniCaseVariants(File extDir) {
        File upperIni = new File(extDir, "POSTAL.INI");
        File lowerIni = new File(extDir, "postal.ini");
        if (!upperIni.exists() && lowerIni.exists()) {
            try {
                copyLocalFile(lowerIni, upperIni);
            } catch (IOException ignored) {
            }
        } else if (upperIni.exists() && !lowerIni.exists()) {
            try {
                copyLocalFile(upperIni, lowerIni);
            } catch (IOException ignored) {
            }
        }
    }

    private static boolean hasBundledAssets(AssetManager am) {
        try {
            String[] rootEntries = am.list("");
            if (rootEntries != null) {
                for (String entry : rootEntries) {
                    if (GAME_DATA_ZIP.equalsIgnoreCase(entry)
                            || "res".equalsIgnoreCase(entry)
                            || "POSTAL.INI".equalsIgnoreCase(entry)) {
                        return true;
                    }
                }
            }
        } catch (IOException ignored) {
        }
        return false;
    }

    /**
     * Shows first-launch extraction progress on screen, unpacks bundled assets on a worker thread,
     * and invokes onComplete on the UI thread once finished.
     */
    public static void extractAsync(final Activity activity, final Runnable onComplete) {
        final File extDir = activity.getExternalFilesDir(null);
        if (extDir == null) {
            onComplete.run();
            return;
        }

        activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.rgb(14, 6, 8));
        int pad = (int) (24f * activity.getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        final TextView titleView = new TextView(activity);
        titleView.setText("Розпакування ресурсів першого запуску...");
        titleView.setTextColor(Color.rgb(240, 232, 232));
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f);
        titleView.setGravity(Gravity.CENTER);
        root.addView(titleView);

        final TextView progressView = new TextView(activity);
        progressView.setText("Підготовка файлів...");
        progressView.setTextColor(Color.rgb(205, 75, 75));
        progressView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f);
        progressView.setGravity(Gravity.CENTER);
        progressView.setPadding(0, pad / 2, 0, 0);
        root.addView(progressView);

        activity.setContentView(root);

        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                final int[] extractedCount = new int[] { 0 };
                try {
                    AssetManager am = activity.getAssets();
                    if (!extDir.exists()) {
                        extDir.mkdirs();
                    }

                    String[] rootEntries = am.list("");
                    String zipAssetName = findAssetIgnoreCase(rootEntries, GAME_DATA_ZIP);

                    if (zipAssetName != null) {
                        extractZipAsset(am, zipAssetName, extDir, activity, progressView, extractedCount);
                    } else if (rootEntries != null) {
                        // Unpack loose asset directories/files (e.g., res/) first
                        for (String entry : rootEntries) {
                            if (isFrameworkAssetDir(entry) || entry.equalsIgnoreCase("POSTAL.INI")) {
                                continue;
                            }
                            extractAssetTree(am, entry, new File(extDir, entry), activity, progressView, extractedCount);
                        }
                    }

                    // Write postal.ini and POSTAL.INI if bundled directly in assets
                    String iniAssetName = findIniAssetName(am, rootEntries);
                    if (iniAssetName != null) {
                        File lowerIni = new File(extDir, "postal.ini");
                        File tempUpper = new File(extDir, "POSTAL.INI.tmp");
                        File upperIni = new File(extDir, "POSTAL.INI");

                        copyAssetFile(am, iniAssetName, lowerIni);
                        copyAssetFile(am, iniAssetName, tempUpper);
                        if (upperIni.exists()) {
                            upperIni.delete();
                        }
                        if (!tempUpper.renameTo(upperIni)) {
                            copyAssetFile(am, iniAssetName, upperIni);
                            tempUpper.delete();
                        }
                    }

                    // Ensure both POSTAL.INI and postal.ini exist on case-sensitive Android storage
                    syncIniCaseVariants(extDir);
                } catch (final Exception e) {
                    android.util.Log.e("PostalExtractor", "Asset extraction failed", e);
                }

                activity.runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        if (!activity.isFinishing() && !activity.isDestroyed()) {
                            onComplete.run();
                        }
                    }
                });
            }
        }, "PostalAssetExtractor");
        worker.start();
    }

    private static void extractZipAsset(
            AssetManager am,
            String zipAssetName,
            File extDir,
            final Activity activity,
            final TextView progressView,
            final int[] extractedCount) throws IOException {

        String canonicalRoot = extDir.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[65536];
        ZipInputStream zis = new ZipInputStream(new BufferedInputStream(am.open(zipAssetName)));
        try {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                File outFile = new File(extDir, entry.getName());
                String canonicalOut = outFile.getCanonicalPath();
                if (!canonicalOut.startsWith(canonicalRoot)) {
                    throw new IOException("Blocked zip-slip entry: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    outFile.mkdirs();
                } else {
                    File parent = outFile.getParentFile();
                    if (parent != null && !parent.exists()) {
                        parent.mkdirs();
                    }
                    FileOutputStream fos = new FileOutputStream(outFile);
                    try {
                        int read;
                        while ((read = zis.read(buffer)) != -1) {
                            fos.write(buffer, 0, read);
                        }
                        fos.flush();
                    } finally {
                        fos.close();
                    }
                    extractedCount[0]++;
                    updateProgressUi(activity, progressView, extractedCount[0], outFile.getName());
                }
                zis.closeEntry();
            }
        } finally {
            zis.close();
        }
    }

    private static void updateProgressUi(
            Activity activity, final TextView progressView, final int count, final String fileName) {
        if (count == 1 || (count % 15 == 0)) {
            activity.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    progressView.setText("Розпаковано файлів: " + count + " (" + fileName + ")");
                }
            });
        }
    }

    private static String findAssetIgnoreCase(String[] entries, String target) {
        if (entries != null) {
            for (String entry : entries) {
                if (target.equalsIgnoreCase(entry)) {
                    return entry;
                }
            }
        }
        return null;
    }

    private static boolean isFrameworkAssetDir(String name) {
        return "images".equals(name)
                || "webkit".equals(name)
                || "sounds".equals(name)
                || "kioskmode".equals(name)
                || "geoid_data".equals(name);
    }

    private static String findIniAssetName(AssetManager am, String[] rootEntries) {
        String match = findAssetIgnoreCase(rootEntries, "POSTAL.INI");
        if (match != null) {
            return match;
        }
        try {
            InputStream is = am.open("POSTAL.INI");
            is.close();
            return "POSTAL.INI";
        } catch (IOException ignored) {
        }
        try {
            InputStream is = am.open("postal.ini");
            is.close();
            return "postal.ini";
        } catch (IOException ignored) {
        }
        return null;
    }

    private static void extractAssetTree(
            AssetManager am,
            String assetPath,
            File destFile,
            final Activity activity,
            final TextView progressView,
            final int[] extractedCount) throws IOException {

        String[] children = am.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destFile.exists()) {
                destFile.mkdirs();
            }
            for (String child : children) {
                extractAssetTree(
                        am,
                        assetPath + "/" + child,
                        new File(destFile, child),
                        activity,
                        progressView,
                        extractedCount);
            }
        } else {
            try {
                copyAssetFile(am, assetPath, destFile);
                extractedCount[0]++;
                updateProgressUi(activity, progressView, extractedCount[0], destFile.getName());
            } catch (IOException e) {
                destFile.mkdirs();
            }
        }
    }

    private static void copyAssetFile(AssetManager am, String assetPath, File destFile) throws IOException {
        File parent = destFile.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        byte[] buffer = new byte[65536];
        InputStream in = null;
        OutputStream out = null;
        try {
            in = am.open(assetPath);
            out = new FileOutputStream(destFile);
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            out.flush();
        } finally {
            if (in != null) {
                try { in.close(); } catch (IOException ignored) {}
            }
            if (out != null) {
                try { out.close(); } catch (IOException ignored) {}
            }
        }
    }

    private static void copyLocalFile(File src, File dst) throws IOException {
        byte[] buffer = new byte[32768];
        InputStream in = null;
        OutputStream out = null;
        try {
            in = new FileInputStream(src);
            out = new FileOutputStream(dst);
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            out.flush();
        } finally {
            if (in != null) {
                try { in.close(); } catch (IOException ignored) {}
            }
            if (out != null) {
                try { out.close(); } catch (IOException ignored) {}
            }
        }
    }
}
