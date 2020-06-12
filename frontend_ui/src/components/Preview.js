import React, {Component} from 'react';
import Slider from '@material-ui/core/Slider';
import PinchZoomPan from "react-responsive-pinch-zoom-pan";

class Preview extends Component {
  state = {
     contrast: 10.0,
     image: 0,
     logarithmic: false
  }

  componentDidMount() {
    this.interval = setInterval(() => this.updateImg(), 500);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  updateImg() {
    this.setState({image: this.state.image + 1});
  }

  sliderMoved = (event, newValue) => {
    this.setState({contrast: newValue});
  };

  render() {
     return <div style={{ width: '80%', margin: 'auto' }}>
        <div style={{ height: '1000px', margin: 'auto'}}>        
        <PinchZoomPan maxScale={4.0}>
        <img src={"http://mx-jungfrau-1:5232/" + (this.state.logarthmic?"preview_log/":"preview/") + this.state.contrast} alt='Preview'/> 
        </PinchZoomPan>
        </div>
        <Slider defaultValue={10.0} min={1.0} max={200} onChange={this.sliderMoved}></Slider>
        </div>;
  }
}

export default Preview;
