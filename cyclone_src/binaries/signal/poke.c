/* Copyright (c) 2003 krzYszcz and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* LATER: 'click' method */


/*dearsic-ing and fragile-ing (plucked the necessary bits from it)
 
  note, the fragile bit was using a way to pluck the float val from a signal
  ignoring usual float-to-signal conversions, this is found in pd's m_obj.c so 
  if something breaks wrt to inlet2, look there first

  Derek Kwan 2016

    needed for that one x->x_indexptr, seems like not many objects use fragile anymore so decided
    to stick it here, plus this is the only one that does this particular thing,
*/

#include "m_pd.h"
#include "cybuf.h"

#define POKE_MAXCHANNELS  64  /* LATER implement arsic resizing feature */
#define POKE_REDRAWMS 500 //redraw time in ms

union inletunion
{
    t_symbol *iu_symto;
    t_gpointer *iu_pointerslot;
    t_float *iu_floatslot;
    t_symbol **iu_symslot;
    t_sample iu_floatsignalvalue;
};

struct _inlet
{
    t_pd i_pd;
    struct _inlet *i_next;
    t_object *i_owner;
    t_pd *i_dest;
    t_symbol *i_symfrom;
    union inletunion i_un;
};

//end extra stuff for x->x_indexptr

typedef struct _poke
{
    t_object x_obj;
    t_cybuf   *x_cybuf;
    t_sample  *x_indexptr;
    t_clock   *x_clock;
    int         x_channum; //current channel number (1-indexed)
    double     x_clocklasttick;
    int        x_clockset;
    double      x_redrawms; //time to redraw in ms

    t_inlet   *x_idxlet;
} t_poke;

static t_class *poke_class;

static void poke_tick(t_poke *x)
{
    cybuf_redraw(x->x_cybuf);  /* LATER redraw only dirty channel(s!) */
    x->x_clockset = 0;
    x->x_clocklasttick = clock_getlogicaltime();
}

static void poke_set(t_poke *x, t_symbol *s)
{
    cybuf_setarray(x->x_cybuf, s);
}

//redraw method/limiter
static void poke_redraw_lim(t_poke *x){
    double redrawms = x->x_redrawms;
    double timesince = clock_gettimesince(x->x_clocklasttick);
    if (timesince > redrawms ) poke_tick(x);
    else if (!x->x_clockset)
    {
	clock_delay(x->x_clock, redrawms - timesince);
	x->x_clockset = 1;
    };

}

static void poke_redraw_force(t_poke *x){
    poke_tick(x);
}

/*
static void poke_bang(t_poke *x)
{
    arsic_redraw((t_arsic *)x);
}
*/

/* CHECKED: index 0-based, negative values block input, overflowed are clipped.
   LATER revisit: incompatibly, the code below is nop for any out-of-range index
   (see also peek.c) */
/* CHECKED: value never clipped, 'clip' not understood */
/* CHECKED: no float-to-signal conversion.  'Float' message is ignored
   when dsp is on -- whether a signal is connected to the left inlet, or not
   (if not, current index is set to zero).  Incompatible (revisit LATER) */
static void poke_float(t_poke *x, t_float f)
{

    t_cybuf *c = x->x_cybuf;
    t_word *vp = c->c_vectors[0];
    //second arg is to allow error posting
    cybuf_validate(c, 1);  /* LATER rethink (efficiency, and complaining) */
    if (vp)
    {
	int ndx = (int)*x->x_indexptr;
	if (ndx >= 0 && ndx < c->c_npts)
	{
	    vp[ndx].w_float = f;
            poke_redraw_lim(x);
	};
    }
}

static void poke_ft2(t_poke *x, t_floatarg f)
{
    int ch = (f < 1) ? 1 : (f > CYBUF_MAXCHANS) ? CYBUF_MAXCHANS : (int) f;
    x->x_channum = ch;
    cybuf_getchannel(x->x_cybuf, ch, 1);
}

static void poke_redraw_rate(t_poke *x, t_floatarg f){
    double redrawms = f > 0 ? (double)f : 1;
    x->x_redrawms = redrawms;
}

static t_int *poke_perform(t_int *w)
{
    t_poke *x = (t_poke *)(w[1]);
    t_cybuf *c = x->x_cybuf;
    int nblock = (int)(w[2]);
    t_float *in1 = (t_float *)(w[3]);
    t_float *in2 = (t_float *)(w[4]);
    t_word *vp = c->c_vectors[0];
    if (vp && c->c_playable)
    {
        poke_redraw_lim(x);
	int npts = c->c_npts;
	while (nblock--)
	{
	    t_float f = *in1++;
	    int ndx = (int)*in2++;
	    if (ndx >= 0 && ndx < npts)
		vp[ndx].w_float = f;
	}
    }
    return (w + 5);
}

static void poke_dsp(t_poke *x, t_signal **sp)
{
    cybuf_checkdsp(x->x_cybuf);
   // arsic_dsp((t_arsic *)x, sp, poke_perform, 0);
    dsp_add(poke_perform, 4, x, sp[0]->s_n, sp[0]->s_vec, sp[1]->s_vec);
}

static void poke_free(t_poke *x)
{
    if (x->x_clock) clock_free(x->x_clock);
    inlet_free(x->x_idxlet);
    cybuf_free(x->x_cybuf);
}

static void *poke_new(t_symbol *s, t_floatarg f)
{
    //like peek~, changing so it doesn't default to 0 but 1 for the new cybuf
    //single channel mode, not sure how much of a diff it makes...
    int ch = (f < 1) ? 1 : (f > CYBUF_MAXCHANS) ? CYBUF_MAXCHANS : (int) f;
	t_poke *x = (t_poke  *)pd_new(poke_class);

    x->x_cybuf = cybuf_init((t_class *) x, s, 1, ch);
    if (x)
    {
        x->x_channum = ch;
        x->x_redrawms = POKE_REDRAWMS; //default redraw rate
	/* CHECKED: no float-to-signal conversion.
	   Floats in 2nd inlet are ignored when dsp is on, but only if a signal
	   is connected to this inlet.  Incompatible (revisit LATER). */
	x->x_idxlet = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);

        //plucked from old unstable/fragile.c. found in pd's m_obj.c, basically plucks the float value
        //from the signal witout the float-to-signal conversion, I think... - DK
	x->x_indexptr = &(x->x_idxlet)->i_un.iu_floatsignalvalue;
	inlet_new((t_object *)x, (t_pd *)x, &s_float, gensym("ft2"));
	x->x_clock = clock_new(x, (t_method)poke_tick);
	x->x_clocklasttick = clock_getlogicaltime();
	x->x_clockset = 0;
    }
    return (x);
}

void poke_tilde_setup(void)
{
    poke_class = class_new(gensym("poke~"),
			   (t_newmethod)poke_new,
			   (t_method)poke_free,
			   sizeof(t_poke), 0,
			   A_DEFSYM, A_DEFFLOAT, 0);
    class_domainsignalin(poke_class, -1);
    class_addfloat(poke_class, poke_float);
    //class_addbang(poke_class, poke_bang); 
    class_addmethod(poke_class, (t_method)poke_dsp, gensym("dsp"), 0);
    class_addmethod(poke_class, (t_method)poke_set,
		    gensym("set"), A_SYMBOL, 0);
    class_addmethod(poke_class, (t_method)poke_ft2,
		    gensym("ft2"), A_FLOAT, 0);
    class_addmethod(poke_class, (t_method)poke_redraw_rate,
		    gensym("redraw_rate"), A_FLOAT, 0);
    class_addmethod(poke_class, (t_method)poke_redraw_force,
		    gensym("redraw"), 0);

}
