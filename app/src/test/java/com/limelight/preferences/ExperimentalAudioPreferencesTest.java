package com.limelight.preferences;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 33, shadows = {com.limelight.shadows.ShadowMoonBridge.class,
        com.limelight.shadows.ShadowGameManager.class})
public class ExperimentalAudioPreferencesTest {
    private Context context;
    private SharedPreferences prefs;

    @Before
    public void setUp() {
        context = ApplicationProvider.getApplicationContext();
        prefs = context.getSharedPreferences("experimental-audio-test", Context.MODE_PRIVATE);
        prefs.edit().clear().commit();
    }

    @Test
    public void upgradingWithoutAnOptInKeepsExperimentalAudioDisabled() {
        prefs.edit().putBoolean("checkbox_enable_audiofx", false).commit();
        PreferenceConfiguration config = PreferenceConfiguration.readPreferences(context, prefs);
        assertFalse(config.forceAndroidLowLatency);
        assertFalse(config.showAudioDiagnostics);
        assertEquals(0, config.lowLatencyAudioBufferMs);
    }

    @Test
    public void audioDiagnosticsCanBeEnabledWithoutEnablingExperimentalAudio() {
        prefs.edit().putBoolean(PreferenceConfiguration.SHOW_AUDIO_DIAGNOSTICS_PREF_STRING, true).commit();
        PreferenceConfiguration config = PreferenceConfiguration.readPreferences(context, prefs);
        assertTrue(config.showAudioDiagnostics);
        assertFalse(config.forceAndroidLowLatency);
    }

    @Test
    public void optInBypassesEffectsWithoutErasingThePreviousEffectsPreference() {
        prefs.edit().putBoolean("checkbox_enable_audiofx", true)
                .putBoolean(PreferenceConfiguration.FORCE_ANDROID_LOW_LATENCY_PREF_STRING, true)
                .putString(PreferenceConfiguration.LOW_LATENCY_AUDIO_BUFFER_PREF_STRING, "32")
                .commit();
        PreferenceConfiguration config = PreferenceConfiguration.readPreferences(context, prefs);
        assertTrue(config.forceAndroidLowLatency);
        assertFalse(config.enableAudioFx);
        assertEquals(32, config.lowLatencyAudioBufferMs);

        prefs.edit().putBoolean(PreferenceConfiguration.FORCE_ANDROID_LOW_LATENCY_PREF_STRING, false)
                .commit();
        config = PreferenceConfiguration.readPreferences(context, prefs);
        assertFalse(config.forceAndroidLowLatency);
        assertTrue(config.enableAudioFx);
    }

    @Test
    public void invalidRestoredBufferValuesUseTheDeviceDefault() {
        for (String value : new String[]{"invalid", "-1", "1", "1000000000"}) {
            prefs.edit().putString(PreferenceConfiguration.LOW_LATENCY_AUDIO_BUFFER_PREF_STRING, value)
                    .commit();
            assertEquals(0, PreferenceConfiguration.readPreferences(context, prefs).lowLatencyAudioBufferMs);
        }
    }
}
