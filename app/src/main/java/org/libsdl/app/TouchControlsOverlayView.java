package org.libsdl.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.DisplayMetrics;
import android.util.SparseIntArray;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

/**
 * True 360-degree analog Twin-Stick + button overlay for Postal 1.
 * Left stick: 360-degree movement (plus automatic DPAD navigation when a menu is active).
 * Right stick: 360-degree aiming + outer >78% auto-fire ring, surrounded by a clean semicircle of action buttons.
 */
public class TouchControlsOverlayView extends View {

    public static native void nativeSetTwinStick(
            float moveX, float moveY, float aimX, float aimY, boolean isFiring);

    public static native boolean nativeIsMenuActive();

    private static final float AUTO_FIRE_THRESHOLD = 0.78f;

    private static final int CONTROL_NONE = -1;
    private static final int CONTROL_HIDE_UI = 1002;
    private static final int BTN_SUICIDE = 3;

    private static class VirtualButton {
        final int id;
        final String label;
        final int keyCode;
        final boolean isServiceButton;
        final boolean isCircular;
        final RectF bounds = new RectF();
        float cx;
        float cy;
        float radius;
        boolean pressed;

        VirtualButton(int id, String label, int keyCode, boolean isServiceButton, boolean isCircular) {
            this.id = id;
            this.label = label;
            this.keyCode = keyCode;
            this.isServiceButton = isServiceButton;
            this.isCircular = isCircular;
        }

        boolean hitTest(float x, float y, float slop) {
            if (isCircular) {
                float dx = x - cx;
                float dy = y - cy;
                float r = radius + slop;
                return (dx * dx + dy * dy) <= (r * r);
            } else {
                return x >= (bounds.left - slop) && x <= (bounds.right + slop)
                        && y >= (bounds.top - slop) && y <= (bounds.bottom + slop);
            }
        }
    }

    private final float mDensity;
    private final float mHitSlop;

    // Pre-allocated Paints & Path (zero allocations in onDraw)
    private final Paint mBaseFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mSuicideFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mPressedFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mFirePressedPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mAutoFireZonePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mAutoFireActivePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mThresholdStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mAccentStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mAimRayPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mKnobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mSmallTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path mAutoFireRingPath = new Path();

    // Left analog joystick (movement + menu D-Pad)
    private float mLeftCenterX;
    private float mLeftCenterY;
    private float mLeftOuterRadius;
    private float mLeftKnobRadius;
    private float mLeftKnobX;
    private float mLeftKnobY;
    private int mLeftPointerId = MotionEvent.INVALID_POINTER_ID;
    private float mMoveX = 0f;
    private float mMoveY = 0f;
    private boolean mDpadUp, mDpadDown, mDpadLeft, mDpadRight;

    // Right analog joystick (360-degree aim + >78% outer auto-fire ring)
    private float mRightCenterX;
    private float mRightCenterY;
    private float mRightOuterRadius;
    private float mRightKnobRadius;
    private float mRightKnobX;
    private float mRightKnobY;
    private int mRightPointerId = MotionEvent.INVALID_POINTER_ID;
    private float mAimX = 0f;
    private float mAimY = 0f;

    // Buttons & UI state
    private final VirtualButton[] mButtons;
    private final VirtualButton mHideUiButton;
    private boolean mCombatUiVisible = true;

    // Multi-touch pointer tracking & reference-counted SDL keys
    private final SparseIntArray mPointerControlMap = new SparseIntArray();
    private final SparseIntArray mActiveKeyCounts = new SparseIntArray();

    public TouchControlsOverlayView(Context context) {
        super(context);
        DisplayMetrics dm = context.getResources().getDisplayMetrics();
        mDensity = dm.density;
        mHitSlop = dp(6);

        mBaseFillPaint.setStyle(Paint.Style.FILL);
        mBaseFillPaint.setColor(Color.argb(88, 18, 16, 18));

        mSuicideFillPaint.setStyle(Paint.Style.FILL);
        mSuicideFillPaint.setColor(Color.argb(105, 70, 10, 12));

        mPressedFillPaint.setStyle(Paint.Style.FILL);
        mPressedFillPaint.setColor(Color.argb(180, 165, 24, 24));

        mFirePressedPaint.setStyle(Paint.Style.FILL);
        mFirePressedPaint.setColor(Color.argb(215, 225, 30, 30));

        // Outer >78% red auto-fire ring fill on Right Stick
        mAutoFireZonePaint.setStyle(Paint.Style.FILL);
        mAutoFireZonePaint.setColor(Color.argb(78, 195, 18, 18));

        mAutoFireActivePaint.setStyle(Paint.Style.FILL);
        mAutoFireActivePaint.setColor(Color.argb(155, 235, 25, 25));

        mThresholdStrokePaint.setStyle(Paint.Style.STROKE);
        mThresholdStrokePaint.setStrokeWidth(dp(1.5f));
        mThresholdStrokePaint.setColor(Color.argb(135, 235, 70, 70));

        mStrokePaint.setStyle(Paint.Style.STROKE);
        mStrokePaint.setStrokeWidth(dp(2f));
        mStrokePaint.setColor(Color.argb(135, 190, 62, 62));

        mAccentStrokePaint.setStyle(Paint.Style.STROKE);
        mAccentStrokePaint.setStrokeWidth(dp(2.5f));
        mAccentStrokePaint.setColor(Color.argb(205, 245, 58, 58));

        mAimRayPaint.setStyle(Paint.Style.STROKE);
        mAimRayPaint.setStrokeWidth(dp(2.5f));
        mAimRayPaint.setColor(Color.argb(175, 255, 90, 90));

        mKnobPaint.setStyle(Paint.Style.FILL);
        mKnobPaint.setColor(Color.argb(165, 185, 34, 34));

        mSmallTextPaint.setColor(Color.argb(210, 242, 232, 232));
        mSmallTextPaint.setTextAlign(Paint.Align.CENTER);
        mSmallTextPaint.setFakeBoldText(true);
        mSmallTextPaint.setTextSize(dp(10.5f));

        mButtons = new VirtualButton[] {
            // Top service bar (left)
            new VirtualButton(0, "ESC", KeyEvent.KEYCODE_ESCAPE, true, false),
            new VirtualButton(1, "ENTER", KeyEvent.KEYCODE_ENTER, true, false),
            new VirtualButton(2, "F1", KeyEvent.KEYCODE_F1, true, false),

            // Top bar Suicide button (placed safely left of HIDE UI, sends KEYCODE_K)
            new VirtualButton(BTN_SUICIDE, "\u2620 K", KeyEvent.KEYCODE_K, false, false),

            // Single clean semicircle of combat buttons around Right Stick
            new VirtualButton(4, "WPN", KeyEvent.KEYCODE_RIGHT_BRACKET, false, true),
            new VirtualButton(5, "GREN", KeyEvent.KEYCODE_4, false, true),
            new VirtualButton(6, "EXEC", KeyEvent.KEYCODE_X, false, true),
            new VirtualButton(7, "DUCK", KeyEvent.KEYCODE_F, false, true),
            new VirtualButton(8, "SPACE", KeyEvent.KEYCODE_SPACE, false, true)
        };

        mHideUiButton = new VirtualButton(CONTROL_HIDE_UI, "HIDE UI", KeyEvent.KEYCODE_UNKNOWN, true, false);
    }

    private float dp(float value) {
        return value * mDensity;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutControls(w, h);
    }

    private void layoutControls(int w, int h) {
        float minDim = Math.min(w, h);

        // 1. Left Analog Stick (bottom-left)
        mLeftOuterRadius = Math.max(dp(68), Math.min(dp(84), minDim * 0.195f));
        mLeftKnobRadius = mLeftOuterRadius * 0.36f;
        mLeftCenterX = dp(28) + mLeftOuterRadius;
        mLeftCenterY = h - dp(24) - mLeftOuterRadius;
        mLeftKnobX = mLeftCenterX;
        mLeftKnobY = mLeftCenterY;

        // 2. Right Analog Aim/Auto-Fire Stick (bottom-right)
        mRightOuterRadius = Math.max(dp(68), Math.min(dp(84), minDim * 0.195f));
        mRightKnobRadius = mRightOuterRadius * 0.34f;
        mRightCenterX = w - dp(28) - mRightOuterRadius;
        mRightCenterY = h - dp(24) - mRightOuterRadius;
        mRightKnobX = mRightCenterX;
        mRightKnobY = mRightCenterY;

        // Precompute annular auto-fire zone path (78%..100% radius)
        mAutoFireRingPath.reset();
        mAutoFireRingPath.setFillType(Path.FillType.EVEN_ODD);
        mAutoFireRingPath.addCircle(mRightCenterX, mRightCenterY, mRightOuterRadius, Path.Direction.CW);
        mAutoFireRingPath.addCircle(mRightCenterX, mRightCenterY, mRightOuterRadius * AUTO_FIRE_THRESHOLD, Path.Direction.CW);

        // 3. Single compact semicircle of 5 buttons hugging the Right Stick (-68 to -188 deg, 30 deg apart)
        float btnR = Math.max(dp(24), Math.min(dp(28), minDim * 0.066f));
        float arcR = mRightOuterRadius + btnR + dp(14);
        float[] arcAnglesDeg = { -68f, -98f, -128f, -158f, -188f };
        for (int i = 0; i < 5; i++) {
            double rad = Math.toRadians(arcAnglesDeg[i]);
            placeCircleButton(
                    mButtons[4 + i],
                    mRightCenterX + (float) Math.cos(rad) * arcR,
                    mRightCenterY + (float) Math.sin(rad) * arcR,
                    btnR);
        }

        // 4. Top Bar Buttons
        float topMargin = dp(12);
        float pillH = dp(32);
        float pillW = dp(56);
        float gap = dp(8);
        float leftStart = dp(18);

        // Top-left: ESC, ENTER, F1
        mButtons[0].bounds.set(leftStart, topMargin, leftStart + pillW, topMargin + pillH);
        float enterLeft = mButtons[0].bounds.right + gap;
        mButtons[1].bounds.set(enterLeft, topMargin, enterLeft + dp(64), topMargin + pillH);
        float f1Left = mButtons[1].bounds.right + gap;
        mButtons[2].bounds.set(f1Left, topMargin, f1Left + dp(48), topMargin + pillH);

        // Top-right: HIDE UI (Eye) and compact Suicide ("☠ K") safely to its left
        float hideW = dp(78);
        float rightEdge = w - dp(18);
        mHideUiButton.bounds.set(rightEdge - hideW, topMargin, rightEdge, topMargin + pillH);

        float suicideW = dp(58);
        float suicideRight = mHideUiButton.bounds.left - dp(12);
        mButtons[BTN_SUICIDE].bounds.set(suicideRight - suicideW, topMargin, suicideRight, topMargin + pillH);
    }

    private void placeCircleButton(VirtualButton btn, float cx, float cy, float radius) {
        btn.cx = cx;
        btn.cy = cy;
        btn.radius = radius;
        btn.bounds.set(cx - radius, cy - radius, cx + radius, cy + radius);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        // 1. Top service bar (always visible)
        for (VirtualButton btn : mButtons) {
            if (btn.isServiceButton) {
                drawPillButton(canvas, btn, btn.label, false);
            }
        }
        String eyeLabel = mCombatUiVisible ? "\uD83D\uDC41 UI" : "\uD83D\uDC41 SHOW";
        drawPillButton(canvas, mHideUiButton, eyeLabel, false);

        // 2. Left Analog Stick (360-deg move + menu D-Pad)
        drawLeftJoystick(canvas);

        // 3. Right Analog Aim/Auto-Fire Stick + Semicircle Combat Buttons + Top Suicide Pill
        if (mCombatUiVisible) {
            drawPillButton(canvas, mButtons[BTN_SUICIDE], mButtons[BTN_SUICIDE].label, true);
            drawRightAimJoystick(canvas);
            for (VirtualButton btn : mButtons) {
                if (!btn.isServiceButton && btn.isCircular) {
                    drawCircleButton(canvas, btn);
                }
            }
        }
    }

    private void drawPillButton(Canvas canvas, VirtualButton btn, String text, boolean isSuicide) {
        float corner = dp(8);
        boolean active = btn.pressed || (btn == mHideUiButton && !mCombatUiVisible);
        Paint fill = active ? mPressedFillPaint : (isSuicide ? mSuicideFillPaint : mBaseFillPaint);
        canvas.drawRoundRect(btn.bounds, corner, corner, fill);
        canvas.drawRoundRect(btn.bounds, corner, corner, active ? mAccentStrokePaint : mStrokePaint);

        Paint.FontMetrics fm = mSmallTextPaint.getFontMetrics();
        float textY = btn.bounds.centerY() - (fm.ascent + fm.descent) * 0.5f;
        canvas.drawText(text, btn.bounds.centerX(), textY, mSmallTextPaint);
    }

    private void drawCircleButton(Canvas canvas, VirtualButton btn) {
        canvas.drawCircle(btn.cx, btn.cy, btn.radius, btn.pressed ? mPressedFillPaint : mBaseFillPaint);
        canvas.drawCircle(btn.cx, btn.cy, btn.radius, btn.pressed ? mAccentStrokePaint : mStrokePaint);

        Paint.FontMetrics fm = mSmallTextPaint.getFontMetrics();
        float textY = btn.cy - (fm.ascent + fm.descent) * 0.5f;
        canvas.drawText(btn.label, btn.cx, textY, mSmallTextPaint);
    }

    private void drawLeftJoystick(Canvas canvas) {
        boolean active = (mLeftPointerId != MotionEvent.INVALID_POINTER_ID);
        canvas.drawCircle(mLeftCenterX, mLeftCenterY, mLeftOuterRadius, mBaseFillPaint);
        canvas.drawCircle(mLeftCenterX, mLeftCenterY, mLeftOuterRadius, active ? mAccentStrokePaint : mStrokePaint);

        if (active) {
            canvas.drawLine(mLeftCenterX, mLeftCenterY, mLeftKnobX, mLeftKnobY, mAimRayPaint);
        }

        canvas.drawCircle(mLeftKnobX, mLeftKnobY, mLeftKnobRadius, active ? mFirePressedPaint : mKnobPaint);
        canvas.drawCircle(mLeftKnobX, mLeftKnobY, mLeftKnobRadius, mStrokePaint);

        if (!active) {
            Paint.FontMetrics fm = mSmallTextPaint.getFontMetrics();
            float textY = mLeftCenterY - (fm.ascent + fm.descent) * 0.5f;
            canvas.drawText("MOVE", mLeftCenterX, textY, mSmallTextPaint);
        }
    }

    private void drawRightAimJoystick(Canvas canvas) {
        boolean active = (mRightPointerId != MotionEvent.INVALID_POINTER_ID);
        float aimMag = (float) Math.hypot(mAimX, mAimY);
        boolean autoFiring = active && (aimMag > AUTO_FIRE_THRESHOLD);

        // Base circle
        canvas.drawCircle(mRightCenterX, mRightCenterY, mRightOuterRadius, mBaseFillPaint);

        // Outer >78% red auto-fire ring
        canvas.drawPath(mAutoFireRingPath, autoFiring ? mAutoFireActivePaint : mAutoFireZonePaint);

        // Inner 78% threshold boundary & outer border
        canvas.drawCircle(mRightCenterX, mRightCenterY, mRightOuterRadius * AUTO_FIRE_THRESHOLD, mThresholdStrokePaint);
        canvas.drawCircle(mRightCenterX, mRightCenterY, mRightOuterRadius,
                autoFiring ? mAccentStrokePaint : mStrokePaint);

        // Aim ray from center to thumb knob
        if (active && aimMag > 0.15f) {
            canvas.drawLine(mRightCenterX, mRightCenterY, mRightKnobX, mRightKnobY, mAimRayPaint);
        }

        // Thumb knob
        canvas.drawCircle(mRightKnobX, mRightKnobY, mRightKnobRadius,
                autoFiring ? mFirePressedPaint : (active ? mPressedFillPaint : mKnobPaint));
        canvas.drawCircle(mRightKnobX, mRightKnobY, mRightKnobRadius, mStrokePaint);

        if (!active) {
            Paint.FontMetrics fm = mSmallTextPaint.getFontMetrics();
            float textY = mRightCenterY - (fm.ascent + fm.descent) * 0.5f;
            canvas.drawText("AIM / FIRE", mRightCenterX, textY, mSmallTextPaint);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int pointerId = event.getPointerId(actionIndex);
                float x = event.getX(actionIndex);
                float y = event.getY(actionIndex);
                boolean handled = handlePointerDown(pointerId, x, y);
                syncNativeTwinStick();
                if (handled || hasAnyActivePointer()) {
                    invalidate();
                    return true;
                }
                return false;
            }

            case MotionEvent.ACTION_MOVE: {
                boolean anyTracked = false;
                int pointerCount = event.getPointerCount();
                for (int i = 0; i < pointerCount; i++) {
                    int pointerId = event.getPointerId(i);
                    float x = event.getX(i);
                    float y = event.getY(i);
                    if (pointerId == mLeftPointerId) {
                        updateLeftStick(x, y);
                        anyTracked = true;
                    } else if (pointerId == mRightPointerId) {
                        updateRightStick(x, y);
                        anyTracked = true;
                    } else if (mPointerControlMap.indexOfKey(pointerId) >= 0) {
                        updateButtonPointerMove(pointerId, x, y);
                        anyTracked = true;
                    }
                }
                syncNativeTwinStick();
                if (anyTracked) {
                    invalidate();
                    return true;
                }
                return false;
            }

            case MotionEvent.ACTION_POINTER_UP: {
                int pointerId = event.getPointerId(actionIndex);
                boolean released = handlePointerUp(pointerId);
                syncNativeTwinStick();
                if (released || hasAnyActivePointer()) {
                    invalidate();
                    return true;
                }
                return false;
            }

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                boolean hadActive = hasAnyActivePointer();
                releaseAllControls();
                syncNativeTwinStick();
                if (hadActive) {
                    invalidate();
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    private boolean hasAnyActivePointer() {
        return (mLeftPointerId != MotionEvent.INVALID_POINTER_ID)
                || (mRightPointerId != MotionEvent.INVALID_POINTER_ID)
                || (mPointerControlMap.size() > 0);
    }

    private boolean handlePointerDown(int pointerId, float x, float y) {
        // 1. HIDE UI toggle button
        if (mHideUiButton.hitTest(x, y, mHitSlop)) {
            mHideUiButton.pressed = true;
            mPointerControlMap.put(pointerId, CONTROL_HIDE_UI);
            toggleCombatUi();
            return true;
        }

        // 2. Service & combat buttons
        for (VirtualButton btn : mButtons) {
            if (!btn.isServiceButton && !mCombatUiVisible) {
                continue;
            }
            if (btn.hitTest(x, y, mHitSlop)) {
                mPointerControlMap.put(pointerId, btn.id);
                setButtonPressed(btn, true);
                return true;
            }
        }

        // 3. Right Analog Aim/Fire Stick (when combat UI is visible)
        if (mCombatUiVisible && mRightPointerId == MotionEvent.INVALID_POINTER_ID) {
            float dx = x - mRightCenterX;
            float dy = y - mRightCenterY;
            float r = mRightOuterRadius * 1.22f;
            if ((dx * dx + dy * dy) <= (r * r)) {
                mRightPointerId = pointerId;
                updateRightStick(x, y);
                return true;
            }
        }

        // 4. Left Analog Move Stick
        if (mLeftPointerId == MotionEvent.INVALID_POINTER_ID) {
            float dx = x - mLeftCenterX;
            float dy = y - mLeftCenterY;
            float r = mLeftOuterRadius * 1.28f;
            if ((dx * dx + dy * dy) <= (r * r)) {
                mLeftPointerId = pointerId;
                updateLeftStick(x, y);
                return true;
            }
        }

        return false;
    }

    private void updateButtonPointerMove(int pointerId, float x, float y) {
        int ctrlId = mPointerControlMap.get(pointerId, CONTROL_NONE);
        if (ctrlId == CONTROL_HIDE_UI || ctrlId == CONTROL_NONE) {
            return;
        }

        VirtualButton currentBtn = (ctrlId >= 0 && ctrlId < mButtons.length) ? mButtons[ctrlId] : null;
        if (currentBtn != null && currentBtn.hitTest(x, y, mHitSlop * 1.6f)) {
            return;
        }

        // Do not slide onto the Suicide button from another button; require a deliberate tap on it
        VirtualButton newBtn = null;
        for (VirtualButton candidate : mButtons) {
            if (candidate.id == BTN_SUICIDE) {
                continue;
            }
            if (!candidate.isServiceButton && !mCombatUiVisible) {
                continue;
            }
            if (candidate.hitTest(x, y, mHitSlop)) {
                newBtn = candidate;
                break;
            }
        }

        int newId = (newBtn != null) ? newBtn.id : CONTROL_NONE;
        if (newId != ctrlId) {
            mPointerControlMap.put(pointerId, newId);
            refreshAllButtonStates();
        }
    }

    private boolean handlePointerUp(int pointerId) {
        if (pointerId == mLeftPointerId) {
            resetLeftStick();
            return true;
        }
        if (pointerId == mRightPointerId) {
            resetRightStick();
            return true;
        }
        int idx = mPointerControlMap.indexOfKey(pointerId);
        if (idx >= 0) {
            int ctrlId = mPointerControlMap.valueAt(idx);
            mPointerControlMap.removeAt(idx);
            if (ctrlId == CONTROL_HIDE_UI) {
                mHideUiButton.pressed = false;
            } else {
                refreshAllButtonStates();
            }
            return true;
        }
        return false;
    }

    private void refreshAllButtonStates() {
        for (VirtualButton btn : mButtons) {
            boolean shouldBePressed = false;
            for (int i = 0; i < mPointerControlMap.size(); i++) {
                if (mPointerControlMap.valueAt(i) == btn.id) {
                    shouldBePressed = true;
                    break;
                }
            }
            setButtonPressed(btn, shouldBePressed);
        }
    }

    private void setButtonPressed(VirtualButton btn, boolean pressed) {
        if (btn.pressed == pressed) {
            return;
        }
        btn.pressed = pressed;
        if (btn.keyCode != KeyEvent.KEYCODE_UNKNOWN) {
            if (pressed) {
                pressKey(btn.keyCode);
            } else {
                releaseKey(btn.keyCode);
            }
        }
    }

    private void updateLeftStick(float x, float y) {
        float dx = x - mLeftCenterX;
        float dy = y - mLeftCenterY;
        float dist = (float) Math.hypot(dx, dy);

        if (dist > mLeftOuterRadius && dist > 0f) {
            float inv = 1f / dist;
            float nx = dx * inv;
            float ny = dy * inv;
            mMoveX = nx;
            mMoveY = ny;
            mLeftKnobX = mLeftCenterX + nx * mLeftOuterRadius;
            mLeftKnobY = mLeftCenterY + ny * mLeftOuterRadius;
        } else {
            mMoveX = dx / mLeftOuterRadius;
            mMoveY = dy / mLeftOuterRadius;
            mLeftKnobX = x;
            mLeftKnobY = y;
        }

        boolean menuMode = !mCombatUiVisible || isMenuActiveSafe();
        float normMag = (float) Math.hypot(mMoveX, mMoveY);
        if (menuMode && normMag >= 0.45f) {
            double deg = Math.toDegrees(Math.atan2(dy, dx));
            if (deg < 0) {
                deg += 360.0;
            }
            int sector = ((int) Math.floor((deg + 22.5) / 45.0)) & 7;
            boolean wantRight = (sector == 0 || sector == 1 || sector == 7);
            boolean wantDown  = (sector == 1 || sector == 2 || sector == 3);
            boolean wantLeft  = (sector == 3 || sector == 4 || sector == 5);
            boolean wantUp    = (sector == 5 || sector == 6 || sector == 7);
            applyMenuDpadState(wantUp, wantDown, wantLeft, wantRight);
        } else {
            applyMenuDpadState(false, false, false, false);
        }
    }

    private void resetLeftStick() {
        mLeftPointerId = MotionEvent.INVALID_POINTER_ID;
        mLeftKnobX = mLeftCenterX;
        mLeftKnobY = mLeftCenterY;
        mMoveX = 0f;
        mMoveY = 0f;
        applyMenuDpadState(false, false, false, false);
    }

    private void updateRightStick(float x, float y) {
        float dx = x - mRightCenterX;
        float dy = y - mRightCenterY;
        float dist = (float) Math.hypot(dx, dy);

        if (dist > mRightOuterRadius && dist > 0f) {
            float inv = 1f / dist;
            float nx = dx * inv;
            float ny = dy * inv;
            mAimX = nx;
            mAimY = ny;
            mRightKnobX = mRightCenterX + nx * mRightOuterRadius;
            mRightKnobY = mRightCenterY + ny * mRightOuterRadius;
        } else {
            mAimX = dx / mRightOuterRadius;
            mAimY = dy / mRightOuterRadius;
            mRightKnobX = x;
            mRightKnobY = y;
        }
    }

    private void resetRightStick() {
        mRightPointerId = MotionEvent.INVALID_POINTER_ID;
        mRightKnobX = mRightCenterX;
        mRightKnobY = mRightCenterY;
        mAimX = 0f;
        mAimY = 0f;
    }

    private void applyMenuDpadState(boolean up, boolean down, boolean left, boolean right) {
        if (up != mDpadUp) {
            mDpadUp = up;
            if (up) pressKey(KeyEvent.KEYCODE_DPAD_UP);
            else releaseKey(KeyEvent.KEYCODE_DPAD_UP);
        }
        if (down != mDpadDown) {
            mDpadDown = down;
            if (down) pressKey(KeyEvent.KEYCODE_DPAD_DOWN);
            else releaseKey(KeyEvent.KEYCODE_DPAD_DOWN);
        }
        if (left != mDpadLeft) {
            mDpadLeft = left;
            if (left) pressKey(KeyEvent.KEYCODE_DPAD_LEFT);
            else releaseKey(KeyEvent.KEYCODE_DPAD_LEFT);
        }
        if (right != mDpadRight) {
            mDpadRight = right;
            if (right) pressKey(KeyEvent.KEYCODE_DPAD_RIGHT);
            else releaseKey(KeyEvent.KEYCODE_DPAD_RIGHT);
        }
    }

    private void toggleCombatUi() {
        mCombatUiVisible = !mCombatUiVisible;
        if (!mCombatUiVisible) {
            resetRightStick();
            for (int i = mPointerControlMap.size() - 1; i >= 0; i--) {
                int ctrlId = mPointerControlMap.valueAt(i);
                if (ctrlId >= 0 && ctrlId < mButtons.length && !mButtons[ctrlId].isServiceButton) {
                    mPointerControlMap.removeAt(i);
                }
            }
            refreshAllButtonStates();
            syncNativeTwinStick();
        }
    }

    private void releaseAllControls() {
        resetLeftStick();
        resetRightStick();
        mHideUiButton.pressed = false;
        mPointerControlMap.clear();
        for (VirtualButton btn : mButtons) {
            setButtonPressed(btn, false);
        }
        for (int i = 0; i < mActiveKeyCounts.size(); i++) {
            int keyCode = mActiveKeyCounts.keyAt(i);
            if (mActiveKeyCounts.valueAt(i) > 0) {
                SDLActivity.onNativeKeyUp(keyCode);
            }
        }
        mActiveKeyCounts.clear();
    }

    private void syncNativeTwinStick() {
        if (SDLActivity.mBrokenLibraries) {
            return;
        }
        try {
            boolean inMenu = isMenuActiveSafe();
            float mx = inMenu ? 0f : mMoveX;
            float my = inMenu ? 0f : mMoveY;
            float ax = (inMenu || !mCombatUiVisible) ? 0f : mAimX;
            float ay = (inMenu || !mCombatUiVisible) ? 0f : mAimY;
            nativeSetTwinStick(mx, my, ax, ay, false);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private boolean isMenuActiveSafe() {
        if (SDLActivity.mBrokenLibraries) {
            return false;
        }
        try {
            return nativeIsMenuActive();
        } catch (UnsatisfiedLinkError ignored) {
            return false;
        }
    }

    private void pressKey(int keyCode) {
        int count = mActiveKeyCounts.get(keyCode, 0);
        if (count == 0) {
            SDLActivity.onNativeKeyDown(keyCode);
        }
        mActiveKeyCounts.put(keyCode, count + 1);
    }

    private void releaseKey(int keyCode) {
        int count = mActiveKeyCounts.get(keyCode, 0);
        if (count <= 1) {
            if (count == 1) {
                SDLActivity.onNativeKeyUp(keyCode);
            }
            mActiveKeyCounts.delete(keyCode);
        } else {
            mActiveKeyCounts.put(keyCode, count - 1);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        releaseAllControls();
        syncNativeTwinStick();
        super.onDetachedFromWindow();
    }
}
